# Apple IIe Emulator (a2e)

I wanted to create a project that would use my MOS6502 library to emulate the 65C02 CPU accurately. I've created a ZX Spectrum emulator in the past, so decided to create an Apple IIe emulator. This was also an excuse to play with modern C++ as well :o)

## Features

- **65C02 CPU Emulation** - Cycle-accurate CMOS 65C02 using the MOS6502 library
- **Authentic Apple IIe Memory Architecture** - Full 128KB (64KB main + 64KB aux), language card, and bank switching
- **Complete Soft Switch Implementation** - All IIe memory management switches (80STORE, RAMRD, RAMWRT, ALTZP, etc.)
- **Text Mode Display** - 40-column text with character ROM rendering and flash support
- **Metal-Accelerated Rendering** - Hardware-accelerated texture display on macOS
- **Keyboard Input** - Full Apple IIe keyboard emulation with proper latch-based strobe handling and key repeat
- **50Hz Timing** - Accurate frame timing at 20,460 CPU cycles per frame
- **Persistent Window State** - Window positions, sizes, and visibility are saved between sessions
- **Modern C++20 Implementation** - Clean, modular architecture with RAII resource management

## Building

```bash
# Initialize and update submodules
git submodule update --init --recursive

# Create build directory
mkdir build
cd build

# Configure and build
cmake ..
cmake --build .

# Run
./bin/a2e
```

## Requirements

- CMake 3.20+
- C++20 compatible compiler (Clang 12+, GCC 10+, MSVC 2019+)
- macOS with Metal support (currently macOS-only for rendering)
- Git (for submodules)

## ROM Files

The emulator requires Apple IIe ROM files to be placed in `include/roms/`:

```
include/roms/
├── CD_ROM.bin          # CD ROM chip (lower 4KB of system ROM)
├── EF_ROM.bin          # EF ROM chip (upper 8KB of system ROM)
└── video/
    └── 341-0160-A.bin  # Character ROM for text display
```

## Architecture Overview

The emulator uses a modular, device-based architecture that closely mirrors the Apple IIe's hardware design.

### Core Components

#### CPU Emulation

The 65C02 CPU runs at 1.023 MHz (1,023,000 cycles/second). At 50Hz display refresh, this means 20,460 cycles per frame. The CPU connects to memory through the MMU via read/write callbacks.

#### Memory Management Unit (MMU)

The MMU handles the complete Apple IIe memory map:

| Address Range | Description                                  |
| ------------- | -------------------------------------------- |
| $0000-$01FF   | Zero Page & Stack (affected by ALTZP)        |
| $0200-$BFFF   | Main RAM (affected by RAMRD/RAMWRT, 80STORE) |
| $C000-$C0FF   | I/O and Soft Switches                        |
| $C100-$CFFF   | Expansion ROM (slot ROM / internal ROM)      |
| $D000-$FFFF   | ROM or Language Card RAM                     |

**Soft Switches Implemented:**

- Memory: 80STORE, RAMRD, RAMWRT, ALTZP, INTCXROM, SLOTC3ROM
- Video: TEXT/GR, MIXED, PAGE1/PAGE2, LORES/HIRES, 80VID, ALTCHAR
- Language Card: Bank selection, read/write enable with pre-write protection

#### Keyboard

The keyboard uses the authentic Apple IIe latch-based model:

- `$C000` - Read latched keycode (bit 7 = key waiting strobe)
- `$C010` - Clear strobe, returns any-key-down status in bit 7

Key repeat is implemented with a 0.5 second initial delay followed by 10 repeats per second.

#### Video Display

The video system renders 40-column text mode (280x192 pixels) using:

- Character ROM (341-0160-A) for glyph rendering
- Green phosphor colors matching classic Apple II monitors
- Support for normal, inverse, and flashing characters
- Metal texture for hardware-accelerated display
- Aspect-ratio-preserving scaling

The video reads directly from main RAM, bypassing MMU soft switches, matching how real Apple IIe hardware works.

### Data Flow

```
Keyboard Input → Video Window → Keyboard Device (latch) → MMU → CPU reads $C000
                                                                      ↓
CPU executes → MMU routes → RAM/ROM ← CPU fetches instructions
                  ↓
            Soft Switches → Update hardware state
                  ↓
Video Window reads RAM → Character ROM lookup → Metal Texture → Display
```

### Component Diagram

```
application
├── CPU (65C02 via MOS6502 library)
│   ├── Read Callback → MMU::read()
│   └── Write Callback → MMU::write()
├── MMU (Memory Management Unit)
│   ├── RAM (64KB main + 64KB aux)
│   ├── ROM (12KB system + 4KB expansion)
│   ├── Keyboard (reference)
│   └── Soft Switch State
├── window_renderer
│   ├── SDL3 Window
│   ├── Metal Device/Queue
│   └── ImGui Context
├── UI Windows
│   ├── video_window (Apple IIe display)
│   ├── cpu_window (register viewer)
│   ├── memory_viewer_window (hex dump)
│   └── text_screen_window (text view)
└── preferences (persistent settings)
```

## User Interface

### Windows

- **Video Display** - Main Apple IIe screen output, accepts keyboard input when focused
- **CPU Registers** - Shows PC, SP, A, X, Y, and status flags
- **Memory Viewer** - Hex dump view of memory with navigation
- **Text Screen** - Alternative text-only view of display memory

### Menu Bar

- **File → Exit** - Quit the emulator
- **View** - Toggle visibility of each window
- **Navigate** - Quick jumps to memory locations (Zero Page, Stack, Text Pages, etc.)

### Keyboard

When the Video Display window is focused, keyboard input is sent to the emulated Apple IIe. Standard keys map to their Apple IIe equivalents:

- Arrow keys, Enter, Escape, Backspace
- All alphanumeric and punctuation keys
- Control key for control characters (Ctrl+A = $01, etc.)

## Configuration

Settings are stored in `~/.config/a2e/`:

- `preferences.ini` - Window visibility states
- `imgui.ini` - Window positions, sizes, and docking layout

These files are created automatically and updated when the application exits.

## Project Structure

```
a2e/
├── external/
│   ├── MOS6502/              # 6502/65C02 CPU emulator library
│   ├── imgui/                # Dear ImGui UI library
│   └── SDL3/                 # SDL3 for windowing and input
├── include/
│   ├── apple2e/
│   │   ├── memory_map.hpp    # Memory address constants
│   │   └── soft_switches.hpp # Soft switch definitions
│   ├── window/
│   │   └── window_renderer.hpp
│   ├── windows/
│   │   ├── base_window.hpp
│   │   ├── cpu_window.hpp
│   │   ├── memory_viewer_window.hpp
│   │   ├── text_screen_window.hpp
│   │   └── video_window.hpp
│   ├── application.hpp
│   ├── bus.hpp
│   ├── device.hpp
│   ├── keyboard.hpp
│   ├── mmu.hpp
│   ├── preferences.hpp
│   ├── ram.hpp
│   ├── rom.hpp
│   └── video.hpp
├── src/
│   ├── apple2e/
│   ├── window/
│   │   └── window_renderer.mm
│   ├── windows/
│   │   ├── cpu_window.cpp
│   │   ├── memory_viewer_window.cpp
│   │   ├── text_screen_window.cpp
│   │   └── video_window.mm
│   ├── application.cpp
│   ├── bus.cpp
│   ├── keyboard.cpp
│   ├── main.cpp
│   ├── mmu.cpp
│   ├── preferences.cpp
│   ├── ram.cpp
│   └── rom.cpp
├── include/roms/              # ROM files (not in repo)
├── tests/                     # Test programs
└── CMakeLists.txt
```

## Current Status

### Implemented

- Complete 65C02 CPU emulation with cycle counting
- Full Apple IIe memory map with bank switching
- All IIe soft switches for memory management
- Language card emulation with pre-write protection
- 40-column text mode with character ROM
- Keyboard input with latch model and key repeat
- Metal-accelerated video display
- 50Hz frame timing
- ImGui-based UI with docking
- Persistent window state

### Planned

- Graphics modes (Lo-res, Hi-res)
- 80-column text mode
- Disk II emulation
- Audio (speaker click, Mockingboard)
- Debugger with breakpoints and single-stepping
- Disassembler window
- Save states

## Tests

Several test programs are included:

```bash
# ROM execution test - verifies ROM loading and basic execution
./build/bin/rom_execution_test

# Keyboard I/O test - tests keyboard latch behavior
./build/bin/keyboard_io_test

# PC stuck diagnostic - checks for CPU execution issues
./build/bin/pc_stuck_diagnostic
```

## License

TBD
