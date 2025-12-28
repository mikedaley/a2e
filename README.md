# Apple IIe Emulator (a2e)

A hobby project to create an Apple IIe emulator using my MOS6502 library. Having built a ZX Spectrum emulator in the past, I wanted to tackle the Apple IIe while also getting more experience with modern C++.

## Features

### CPU & Memory

- **65C02 CPU** - Cycle-accurate CMOS 65C02 emulation via the MOS6502 library (1.023 MHz)
- **128KB Memory** - Full Apple IIe memory with 64KB main + 64KB auxiliary RAM
- **Complete Soft Switches** - All IIe memory management (80STORE, RAMRD, RAMWRT, ALTZP, INTCXROM, SLOTC3ROM, etc.)
- **Language Card** - Full $D000-$FFFF bank switching with two $D000 banks

### Display

- **Text Modes** - 40-column and 80-column text (40x24 / 80x24)
- **Graphics Modes** - Hi-res (280x192), Lo-res (40x48 with 16 colors), and mixed mode
- **Video Standards** - NTSC with artifact colors, NTSC with color fringing, PAL color (TCA650) - Experimental, PAL monochrome
- **Text Colors** - Green phosphor and white text options
- **Character ROM** - Primary and alternate character sets with flashing character support
- **Metal Rendering** - Hardware-accelerated display on macOS

### Disk II Controller

- **Dual Drives** - Two disk drives with full stepper motor emulation
- **Disk Formats** - WOZ 1.0/2.0, DSK, DO (DOS order), PO (ProDOS order)
- **Full Read/Write** - GCR encoding/decoding with 6-and-2 nibble translation
- **Create Disks** - Create new blank DOS 3.3 formatted disks from the UI
- **Auto-Save** - Automatic saving on eject with backup creation

### Audio

- **Speaker Emulation** - 1-bit toggle speaker at $C030
- **Audio Output** - PortAudio with 48kHz sample rate and ring buffer
- **Volume Control** - Adjustable volume with mute/unmute
- **Audio Sync** - Audio-driven timing for cycle-accurate emulation

### Input

- **Keyboard** - Apple IIe keyboard with latch handling, strobe, and key repeat
- **Reset** - Warm reset (Ctrl+Reset) and hard reset support

### Debugger

- **Breakpoints** - Execution breakpoints with enable/disable
- **Watchpoints** - Memory read and write watchpoints
- **Disassembly** - Live disassembly view with PC tracking
- **Execution Control** - Run, pause, step over, step out
- **Memory Visualization** - 256x256 pixel map showing memory access patterns (read/write)

### Persistent State

- Window positions and sizes saved between sessions
- Emulator state save/load

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
- PortAudio (`brew install portaudio`)
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

The emulator supports multiple disk image formats:

- **WOZ 1.0/2.0** - Bit-accurate disk images with variable track lengths
- **DSK** - Standard 140KB DOS order images
- **DO** - DOS order images (same as DSK)
- **PO** - ProDOS order images

Use the Disk Activity window to:

- Load disk images (drag files or use the file browser)
- Create new blank DOS 3.3 formatted disks
- Eject disks (automatically saves any changes with backup)

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

- **Video Display** - Apple IIe screen with keyboard input capture
- **CPU Monitor** - Live view of PC, SP, A, X, Y, flags, stack preview, and cycle counter
- **Debugger** - Disassembly view, breakpoint management, execution controls (step, run, pause)
- **Memory Viewer** - Full 64KB hex editor with jump-to-address
- **Memory Access** - 256x256 visualization of memory read/write activity with zoom
- **Soft Switches** - Current state of all soft switches and video modes
- **Disk Activity** - Drive status, track position, phase magnets, load/eject/create controls
- **Log** - Structured logging output with category filtering

## Configuration

Settings stored in `~/.config/a2e/`:

- `preferences.ini` - Window visibility
- `imgui.ini` - Window layout

## Current Limitations

- macOS only (Metal rendering)
- No joystick/paddle support
- No double hi-res graphics
- No cassette I/O
- No printer support

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
