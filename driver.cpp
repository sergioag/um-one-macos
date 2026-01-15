#include <csignal>
#include <iostream>
#include <vector>

#include "usb_device.h"
#include <portmidi.h>
#include <porttime.h>

#define OUTPUT_BUFFER_SIZE 0
#define INPUT_BUFFER_SIZE 10
#define TIME_PROC ((PmTimeProcPtr)Pt_Time)
#define TIME_INFO nullptr

UsbDevice* usbDevice = nullptr;
PmStream* inputPort = nullptr;
PmStream* outputPort = nullptr;
int inputPortId = -1;
int outputPortId = -1;
volatile bool exiting = false;

void signalHandler(int /*sig*/) {
  exiting = true;
}

void flushSysexToUSB(std::vector<unsigned char>& sysexOutBytes) {
  std::vector<unsigned char> sysexSendBytes;
  for (size_t j = 0; j < sysexOutBytes.size(); j += 3) {
    if (j == sysexOutBytes.size() - 3) {
      sysexSendBytes.push_back(0x07);
      sysexSendBytes.push_back(sysexOutBytes[j + 0]);
      sysexSendBytes.push_back(sysexOutBytes[j + 1]);
      sysexSendBytes.push_back(sysexOutBytes[j + 2]);
    } else if (j == sysexOutBytes.size() - 2) {
      sysexSendBytes.push_back(0x06);
      sysexSendBytes.push_back(sysexOutBytes[j + 0]);
      sysexSendBytes.push_back(sysexOutBytes[j + 1]);
    } else if (j == sysexOutBytes.size() - 1) {
      sysexSendBytes.push_back(0x05);
      sysexSendBytes.push_back(sysexOutBytes[j + 0]);
    } else {
      sysexSendBytes.push_back(0x04);
      sysexSendBytes.push_back(sysexOutBytes[j + 0]);
      sysexSendBytes.push_back(sysexOutBytes[j + 1]);
      sysexSendBytes.push_back(sysexOutBytes[j + 2]);
    }
  }

  int actualLength = 0;
  usbDevice->write(&sysexSendBytes[0],
                   static_cast<int>(sysexSendBytes.size()), &actualLength, 1);
  sysexOutBytes.clear();

  // printf("Sending sysex: ");
  // for (size_t i = 0; i < sysexSendBytes.size(); i++) {
  //   printf("%02x ", sysexSendBytes[i]);
  // }
  // printf("\n");
}

void flushSysexToHost(std::vector<unsigned char>& sysexInBytes) {
  if (sysexInBytes.size() < 4) {
    sysexInBytes.push_back(0);
    sysexInBytes.push_back(0);
    sysexInBytes.push_back(0);
    sysexInBytes.push_back(0);
  }

  PmEvent event;
  event.timestamp = Pt_Time();
  event.message = (sysexInBytes[0] << 0) | (sysexInBytes[1] << 8) |
                  (sysexInBytes[2] << 16) | (sysexInBytes[3] << 24);
  Pm_Write(outputPort, &event, 1);

  sysexInBytes.clear();
}

void pushSysexToUSB(const unsigned char byte, std::vector<unsigned char>& sysexOutBytes) {
  if (sysexOutBytes.size() == 48) {
    flushSysexToUSB(sysexOutBytes);
  }
  sysexOutBytes.push_back(byte);
}

void endSysexToUSB(const unsigned char byte, std::vector<unsigned char>& sysexOutBytes) {
  if (sysexOutBytes.size() == 48) {
    flushSysexToUSB(sysexOutBytes);
  }
  sysexOutBytes.push_back(byte);
  flushSysexToUSB(sysexOutBytes);
}

void deviceLoop() {
  PmEvent buffer[1];

  // Arbitrarily big buffer
  constexpr int sendBufferNEvents = 1024;
  unsigned char sendBuffer[sendBufferNEvents * 4] = {};

  bool sysexSendMode = false;
  std::vector<unsigned char> sysexOutBytes;
  std::vector<unsigned char> sysexInBytes;

  exiting = false;

  while (!exiting) {
    // Read from virtual port
    const int length = Pm_Read(inputPort, buffer, sendBufferNEvents);
    if (length == pmBufferOverflow || length > sendBufferNEvents) {
      std::cout << "Buffer overflow!" << std::endl;
    }

    if (length > 0) {
      for (size_t j = 0; j < length; j++) {
        // printf("Got message: time %ld, %02x %02x %02x %02x\n",
        //        (long)buffer[i].timestamp,
        //        Pm_MessageStatus(buffer[i].message),
        //        Pm_MessageData1(buffer[i].message),
        //        Pm_MessageData2(buffer[i].message),
        //        (buffer[i].message >> 24) & 0xFF);

        if (!sysexSendMode && Pm_MessageStatus(buffer[j].message) == 0xF0) {
          sysexSendMode = true;
        }

        if (sysexSendMode) {
          if (const auto b1 = (buffer[j].message >> 0) & 0xFF; b1 != 0xF0 && b1 & 0b10000000) {
            endSysexToUSB(b1, sysexOutBytes);
            sysexSendMode = false;
          } else if (sysexSendMode) {
            pushSysexToUSB(b1, sysexOutBytes);
          }

          if (const auto b2 = (buffer[j].message >> 8) & 0xFF; b2 != 0xF0 && b2 & 0b10000000) {
            endSysexToUSB(b2, sysexOutBytes);
            sysexSendMode = false;
          } else if (sysexSendMode) {
            pushSysexToUSB(b2, sysexOutBytes);
          }

          if (const auto b3 = (buffer[j].message >> 16) & 0xFF; b3 != 0xF0 && b3 & 0b10000000) {
            endSysexToUSB(b3, sysexOutBytes);
            sysexSendMode = false;
          } else if (sysexSendMode) {
            pushSysexToUSB(b3, sysexOutBytes);
          }

          if (const auto b4 = (buffer[j].message >> 24) & 0xFF; b4 != 0xF0 && b4 & 0b10000000) {
            endSysexToUSB(b4, sysexOutBytes);
            sysexSendMode = false;
          } else if (sysexSendMode) {
            pushSysexToUSB(b4, sysexOutBytes);
          }
        } else {
          sendBuffer[j * 4 + 0] =
              (Pm_MessageStatus(buffer[j].message) & 0xF0) >> 4;
          sendBuffer[j * 4 + 1] = Pm_MessageStatus(buffer[j].message);
          sendBuffer[j * 4 + 2] = Pm_MessageData1(buffer[j].message);
          sendBuffer[j * 4 + 3] = Pm_MessageData2(buffer[j].message);
        }
      }

      int actualLength = 0;
      /*result = */usbDevice->write(sendBuffer, length * 4,
                                    &actualLength, 1);
      // std::cout << "Result: " << libusb_error_name(result) <<
      // std::endl; std::cout << "Actual length: " << actualLength <<
      // std::endl;
    }

    // Read from USB
    int actualLength = 0;
    unsigned char readBuffer[64] = {};
    const UsbError result = usbDevice->read(readBuffer,
                                  sizeof(readBuffer), &actualLength, 1);
    // std::cout << "Read: " << libusb_error_name(result) << std::endl;
    if (result == UsbError::Success && actualLength > 0) {
      // for (size_t i = 0; i < actualLength; i++) {
      //   printf("%02X", readBuffer[i]);
      // }
      // std::cout << std::endl;

      for (size_t j = 0; j < actualLength / 4; j++) {
        const auto controlByte = readBuffer[j * 4];
        auto b1 = readBuffer[j * 4 + 1];
        auto b2 = readBuffer[j * 4 + 2];
        auto b3 = readBuffer[j * 4 + 3];
        // printf("%02x %02x %02x %02x\n", controlByte, b1, b2, b3);

        if (controlByte == 0x04) {
          sysexInBytes.push_back(b1);
          if (sysexInBytes.size() == 4)
            flushSysexToHost(sysexInBytes);
          sysexInBytes.push_back(b2);
          if (sysexInBytes.size() == 4)
            flushSysexToHost(sysexInBytes);
          sysexInBytes.push_back(b3);
          if (sysexInBytes.size() == 4)
            flushSysexToHost(sysexInBytes);
        } else if (controlByte >= 0x05 && controlByte <= 0x07) {
          sysexInBytes.push_back(b1);
          if (sysexInBytes.size() == 4)
            flushSysexToHost(sysexInBytes);
          sysexInBytes.push_back(b2);
          if (sysexInBytes.size() == 4)
            flushSysexToHost(sysexInBytes);
          sysexInBytes.push_back(b3);
          flushSysexToHost(sysexInBytes);
        } else {
          PmEvent event;
          event.timestamp = Pt_Time();
          event.message = (readBuffer[j * 4 + 1] << 0) |
                          (readBuffer[j * 4 + 2] << 8) |
                          (readBuffer[j * 4 + 3] << 16);
          Pm_Write(outputPort, &event, 1);
        }
      }
    }
  }
}


int initPorts() {
  Pm_Initialize();

  // allocate some space we will alias with open-ended PmDriverInfo:
  static char dimem[sizeof(PmSysDepInfo) + sizeof(void *) * 2];
  auto *sysdepinfo = reinterpret_cast<PmSysDepInfo *>(dimem);
  // build the driver info structure:
  sysdepinfo->structVersion = PM_SYSDEPINFO_VERS;
  sysdepinfo->length = 1;
  sysdepinfo->properties[0].key = pmKeyCoreMidiManufacturer;
  const auto strRoland = "Roland";
  sysdepinfo->properties[0].value = strRoland;

  inputPortId = Pm_CreateVirtualInput("UM-ONE", nullptr, sysdepinfo);
  if (inputPortId < 0) {
    return -1;
  }
  outputPortId = Pm_CreateVirtualOutput("UM-ONE", nullptr, sysdepinfo);
  if (outputPortId < 0) {
    Pm_DeleteVirtualDevice(inputPortId);
    return -2;
  }

  PmEvent buffer[1];
  Pm_OpenInput(&inputPort, inputPortId, nullptr, 0, nullptr, nullptr);
  Pm_OpenOutput(&outputPort, outputPortId, nullptr, OUTPUT_BUFFER_SIZE, TIME_PROC,
                TIME_INFO, 0);
  std::cout << "Created/Opened input " << inputPortId << " and output " << outputPortId << std::endl;
  Pm_SetFilter(inputPort, PM_FILT_ACTIVE | PM_FILT_CLOCK);

  // Empty the buffer, just in case anything got through
  while (Pm_Poll(inputPort)) {
    Pm_Read(outputPort, buffer, 1);
  }

  return 0;
}

int main() {

  usbDevice = new UsbDevice(0x0582, 0x012A);
  UsbError result = usbDevice->connect();

  if (result != UsbError::Success) {
    if (result == UsbError::DeviceNotFound) {
      std::cout << "Device not found" << std::endl;
    } else if (result == UsbError::OpenFailed) {
      std::cout << "Cannot open device" << std::endl;
    } else {
      std::cout << "Generic USB error" << std::endl;
    }
    delete usbDevice;
    return 1;
  }

  if (initPorts() >= 0) {
    signal(SIGINT, signalHandler);
    deviceLoop();
  }
  else {
    std::cout << "Failed to initialize PortMIDI ports" << std::endl;
  }

  Pm_Close(outputPort);
  Pm_Close(inputPort);
  Pm_DeleteVirtualDevice(inputPortId);
  Pm_DeleteVirtualDevice(outputPortId);
  Pm_Terminate();

  delete usbDevice;
  return 0;
}
