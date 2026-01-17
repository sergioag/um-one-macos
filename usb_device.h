#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <cstdint>

enum class UsbError {
    Success = 0,
    InitFailed,
    NoDeviceList,
    DeviceNotFound,
    OpenFailed,
    ClaimInterfaceFailed,
    TransferFailed,
    Timeout,
    NotConnected
};

class UsbDevice {
public:
    UsbDevice(uint16_t vendorId, uint16_t productId);
    ~UsbDevice();

    // Prevent copying
    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    UsbError connect();
    void disconnect();
    [[nodiscard]] bool isConnected() const;

    UsbError read(unsigned char* buffer, int size, int* actualLength, unsigned int timeout) const;
    UsbError write(unsigned char* buffer, int size, int* actualLength, unsigned int timeout) const;

private:
    uint16_t vendorId_;
    uint16_t productId_;
    void* deviceHandle_; // Opaque pointer to hide libusb types
    bool contextInitialized_;

    static constexpr unsigned char readEndpoint_ = 0x81;
    static constexpr unsigned char writeEndpoint_ = 0x02;
    static constexpr int interfaceNumber_ = 0;
};

#endif // USB_DEVICE_H

