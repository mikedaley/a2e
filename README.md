# Apple IIe Emulator (a2e)

A hobby project to create an Apple IIe emulator using my MOS6502 library. Having built a ZX Spectrum emulator in the past, I wanted to tackle the Apple IIe while also getting more experience with modern C++.

## Features

- **65C02 CPU** - Cycle-accurate CMOS 65C02 emulation via the MOS6502 library
- **128KB Memory** - Full Apple IIe memory with 64KB main + 64KB auxiliary RAM
- **Complete Soft Switches** - All IIe memory management (80STORE, RAMRD, RAMWRT, ALTZP, etc.)
- **Disk II Controller** - Full read/write support with WOZ 1.0/2.0 disk images
- **Disk Management** - Create new DOS 3.3 formatted disks, INIT, SAVE, and CATALOG support
- **Text Display** - 40-column text with character ROM, inverse, and flash
- **Metal Rendering** - Hardware-accelerated display on macOS
- **Keyboard** - Apple IIe keyboard with proper latch handling and key repeat
- **Speaker** - Toggle-based audio with SDL output
- **Persistent State** - Window positions and sizes saved between sessions

## Building

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake ..
cmake --build .
./bin/a2e
```

## Requirements

- CMake 3.20+
- C++20 compiler (Clang 12+, GCC 10+, MSVC 2019+)
- macOS with Metal support
- Git

## ROM Files

Place Apple IIe ROMs in `resources/roms/`:

```
resources/roms/
├── 341-0135-A.bin      # CD ROM (4KB)
├── 341-0134-A.bin      # EF ROM (8KB)
├── disk/
│   └── 341-0027.bin    # Disk II controller ROM
└── video/
    └── 341-0160-A.bin  # Character ROM
```

## Usage

### Disk Images

The emulator supports WOZ format disk images (WOZ 1.0 and 2.0). Use the Disk Activity window to:

- Load existing WOZ disk images
- Create new blank DOS 3.3 formatted disks
- Eject disks (automatically saves any changes)

### DOS 3.3 Commands

Once booted into DOS 3.3:

- `CATALOG` - List files on disk
- `INIT HELLO` - Format a blank disk with DOS
- `SAVE filename` - Save current BASIC program
- `LOAD filename` - Load a BASIC program
- `RUN filename` - Load and run a program

### Keyboard

Standard keys map to Apple IIe equivalents. Control key works for control characters (Ctrl+C, etc.).

## Windows

- **Video Display** - Apple IIe screen, receives keyboard input when focused
- **CPU Registers** - Live view of PC, SP, A, X, Y, and flags
- **Memory Viewer** - Hex dump with navigation
- **Soft Switches** - Current state of all soft switches
- **Disk Activity** - Drive status, track position, load/eject controls

## Configuration

Settings stored in `~/.config/a2e/`:
- `preferences.ini` - Window visibility
- `imgui.ini` - Window layout

## Current Limitations

- Text mode only (no graphics modes yet)
- 40-column only (no 80-column mode)
- macOS only (Metal rendering)
- No joystick support

## Project Structure

```
a2e/
├── external/           # MOS6502, ImGui, SDL3
├── include/
│   ├── emulator/       # Core emulation (CPU, MMU, Disk II, etc.)
│   └── ui/             # Window classes
├── src/
│   ├── emulator/       # Implementation
│   └── ui/             # UI implementation
└── resources/roms/     # ROM files (not included)
```

## License

TBD
