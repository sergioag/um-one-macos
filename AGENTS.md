# AGENTS.md

## Project Overview

This project implements a macOS driver for the Roland UM-ONE MIDI interface, enabling bidirectional MIDI communication between a host computer and the device over USB. It utilizes `libusb` for USB communication and `PortMidi` for virtual MIDI port management.

## Build System

- **Language**: C++23 or later
- **Dependencies**:
    - `libusb-1.0` - USB device communication
    - `portmidi` - MIDI port management
    - `porttime` - Timing functions for MIDI events

## Architecture

### Main Components

- `main()` - Entry point, USB device discovery and initialization
- `deviceLoop()` - Main event loop handling bidirectional MIDI/USB communication
- `openDevice()` - USB device handle acquisition and interface claiming
- `initPorts()` - PortMidi virtual port creation

### Data Flow

1. **Host → Device**: Read from virtual MIDI input port → Convert to USB MIDI packets → Send via bulk transfer
2. **Device → Host**: Read USB bulk transfer → Parse USB MIDI packets → Write to virtual MIDI output port

### SysEx Handling

- Special handling for System Exclusive messages with USB MIDI packet types 0x04-0x07
- Buffered transmission with 48-byte chunks

## Hardware

- **Target Device**: Roland UM-ONE (VID: 0x0582, PID: 0x012A)
- **USB Endpoints**: 0x02 (OUT), 0x81 (IN)

## Code Conventions

- Use `constexpr` for compile-time constants
- Lambda functions for scoped helper operations
- Signal handling via `volatile bool` flag for graceful shutdown

## Testing

<!-- Add testing instructions here -->

## Known Limitations

<!-- Document any known issues or limitations -->
