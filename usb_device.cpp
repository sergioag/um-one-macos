#include "usb_device.h"
#include <cstdint>
#include <libusb-1.0/libusb.h>

UsbDevice::UsbDevice(uint16_t vendorId, uint16_t productId)
    : vendorId_(vendorId)
    , productId_(productId)
    , deviceHandle_(nullptr)
    , contextInitialized_(false) {
}

UsbDevice::~UsbDevice() {
    disconnect();
}

UsbError UsbDevice::connect() {
    if (deviceHandle_) {
        return UsbError::Success; // Already connected
    }

    if (libusb_init_context(nullptr, nullptr, 0) != LIBUSB_SUCCESS) {
        return UsbError::InitFailed;
    }
    contextInitialized_ = true;

    libusb_device** devs;
    ssize_t cnt = libusb_get_device_list(nullptr, &devs);
    if (cnt < 0) {
        libusb_exit(nullptr);
        contextInitialized_ = false;
        return UsbError::NoDeviceList;
    }

    libusb_device* targetDevice = nullptr;
    for (ssize_t i = 0; devs[i]; i++) {
        libusb_device_descriptor desc{};
        libusb_get_device_descriptor(devs[i], &desc);
        if (desc.idVendor == vendorId_ && desc.idProduct == productId_) {
            targetDevice = devs[i];
            break;
        }
    }

    if (!targetDevice) {
        libusb_free_device_list(devs, 1);
        libusb_exit(nullptr);
        contextInitialized_ = false;
        return UsbError::DeviceNotFound;
    }

    libusb_device_handle* handle = nullptr;
    if (libusb_open(targetDevice, &handle) != LIBUSB_SUCCESS || !handle) {
        libusb_free_device_list(devs, 1);
        libusb_exit(nullptr);
        contextInitialized_ = false;
        return UsbError::OpenFailed;
    }

    libusb_free_device_list(devs, 1);

    if (libusb_claim_interface(handle, interfaceNumber_) != LIBUSB_SUCCESS) {
        libusb_close(handle);
        libusb_exit(nullptr);
        contextInitialized_ = false;
        return UsbError::ClaimInterfaceFailed;
    }

    deviceHandle_ = handle;
    return UsbError::Success;
}

void UsbDevice::disconnect() {
    if (deviceHandle_) {
        auto* handle = static_cast<libusb_device_handle*>(deviceHandle_);
        libusb_release_interface(handle, interfaceNumber_);
        libusb_close(handle);
        deviceHandle_ = nullptr;
    }
    if (contextInitialized_) {
        libusb_exit(nullptr);
        contextInitialized_ = false;
    }
}

bool UsbDevice::isConnected() const {
    return deviceHandle_ != nullptr;
}

UsbError UsbDevice::read(unsigned char* buffer, int size, int* actualLength, unsigned int timeout) {
    if (!deviceHandle_) {
        return UsbError::NotConnected;
    }

    auto* handle = static_cast<libusb_device_handle*>(deviceHandle_);
    int result = libusb_bulk_transfer(handle, readEndpoint_, buffer, size, actualLength, timeout);

    if (result == LIBUSB_SUCCESS) {
        return UsbError::Success;
    } else if (result == LIBUSB_ERROR_TIMEOUT) {
        return UsbError::Timeout;
    }
    return UsbError::TransferFailed;
}

UsbError UsbDevice::write(unsigned char* buffer, int size, int* actualLength, unsigned int timeout) {
    if (!deviceHandle_) {
        return UsbError::NotConnected;
    }

    auto* handle = static_cast<libusb_device_handle*>(deviceHandle_);
    int result = libusb_bulk_transfer(handle, writeEndpoint_, buffer, size, actualLength, timeout);

    if (result == LIBUSB_SUCCESS) {
        return UsbError::Success;
    } else if (result == LIBUSB_ERROR_TIMEOUT) {
        return UsbError::Timeout;
    }
    return UsbError::TransferFailed;
}

