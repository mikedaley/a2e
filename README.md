# Apple IIe Emulator (a2e)

I wanted to create a project that would use my MOS6502 library to emulate the 65C02 CPU accurately. I've created a ZX Spectrum emulator in the past, so decided to create an Apple IIe emulator. This was also an excuse to play with modern C++ as well :o)

## Features

- **65C02 CPU Emulation** - Cycle-accurate CMOS 65C02 using the MOS6502 library
- **Authentic Apple IIe Memory Architecture** - Full 128KB (64KB main + 64KB aux), language card, and bank switching
- **Complete Soft Switch Implementation** - All IIe memory management switches (80STORE, RAMRD, RAMWRT, ALTZP, etc.)
- **Disk II Controller Emulation** - Full Disk II card emulation in slot 6 with support for .dsk disk images
- **Disk Image Support** - Load and boot from DOS 3.3 disk images with automatic 6-and-2 nibble encoding
- **Text Mode Display** - 40-column text with character ROM rendering and flash support
- **Metal-Accelerated Rendering** - Hardware-accelerated texture display on macOS
- **Keyboard Input** - Full Apple IIe keyboard emulation with proper latch-based strobe handling and key repeat
- **Speaker Audio** - Toggle-based speaker emulation with SDL audio output
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
├── disk/
│   └── P5_DISK2.bin    # Disk II controller ROM (slot 6, 256 bytes)
└── video/
    └── 341-0160-A.bin  # Character ROM for text display
```

The emulator will auto-load `Apple DOS 3.3 January 1983.dsk` on startup if present in the project root.

## Usage

### Running the Emulator

```bash
./build/bin/a2e
```

The emulator starts with:
- DOS 3.3 auto-loaded in Drive 1 (if available)
- Video display window showing Apple IIe screen
- CPU window showing registers
- Disk activity window for managing disks

### Loading Disk Images

**Method 1: Auto-load on startup**
- Place a disk image named `Apple DOS 3.3 January 1983.dsk` in the project root
- It will be automatically loaded into Drive 1 on startup

**Method 2: Load via UI**
- Open the "Disk Activity" window (View → Disk Activity)
- Click "Load..." button for Drive 1 or Drive 2
- Select a `.dsk` file from the file browser
- The disk image will be encoded to nibbles and inserted

**Method 3: Programmatic**
```cpp
auto disk_image = std::make_unique<DiskImage>();
if (disk_image->loadFromDSK("path/to/disk.dsk")) {
    disk_controller->insertDisk(0, std::move(disk_image)); // Drive 1
}
```

### Booting DOS 3.3

Once DOS 3.3 is loaded:
1. The boot ROM automatically seeks to track 0
2. Loads boot sector into memory
3. Executes boot code
4. DOS 3.3 prompt appears: `]`

Common DOS 3.3 commands:
- `CATALOG` - List files on disk
- `RUN filename` - Load and run a BASIC program
- `LOAD filename` - Load a BASIC program
- `BRUN filename` - Load and run a binary program

### Ejecting Disks

- Click "Eject" button in Disk Activity window
- Or programmatically: `disk_controller->ejectDisk(0)`

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

#### Disk II Controller

The Disk II controller emulation provides authentic floppy disk support in slot 6 (`$C0E0-$C0EF`).

**Hardware Emulation:**

- **Stepper Motor** - Quarter-track resolution head positioning (tracks 0-34, 140 quarter-tracks total)
- **Phase Control** - 4-phase stepper motor with magnet-to-position lookup tables from apple2ts
- **Drive Motor** - Motor on/off control with state tracking
- **Dual Drives** - Support for Drive 1 and Drive 2
- **Read/Write Modes** - Q6 (shift/load) and Q7 (read/write) control logic
- **Data Latch** - 8-bit latch for nibble data transfer
- **Write Protection** - Read-only disk image support

**Soft Switches ($C0E0-$C0EF):**

| Address | Function                                   |
| ------- | ------------------------------------------ |
| $C0E0/1 | Phase 0 Off/On (stepper motor)             |
| $C0E2/3 | Phase 1 Off/On                             |
| $C0E4/5 | Phase 2 Off/On                             |
| $C0E6/7 | Phase 3 Off/On                             |
| $C0E8/9 | Motor Off/On                               |
| $C0EA/B | Drive 1/Drive 2 Select                     |
| $C0EC   | Q6L - Shift data register (trigger read)   |
| $C0ED   | Q6H - Load register / sense write protect  |
| $C0EE   | Q7L - Read mode                            |
| $C0EF   | Q7H - Write mode                           |

**Disk Image Format:**

The emulator supports standard `.dsk` files (143,360 bytes):

- **35 tracks** per disk (tracks 0-34)
- **16 sectors** per track (sectors 0-15)
- **256 bytes** per sector
- **6-and-2 Nibble Encoding** - DOS 3.3 sector data is automatically encoded to nibbles on load

Each track is stored as **6656 nibbles** in memory:
- Address field (prologue, volume, track, sector, checksum, epilogue)
- Data field (prologue, 342 nibbles of 6-and-2 encoded data, checksum, epilogue)
- Self-sync bytes ($FF) between fields

The nibble encoding process:
1. Reads 256-byte sectors from .dsk file
2. Converts to 6-and-2 encoding (256 bytes → 342 nibbles with bit 7 always set)
3. Wraps in address and data field markers
4. Fills gaps with self-sync bytes

**Head Movement:**

The stepper motor uses authentic phase-to-position logic:
- 4 phase magnets control head position
- Each position change moves by quarter-tracks
- Direction determined by magnet activation sequence
- Clamped to valid range (0-139 quarter-tracks)

**Read Operation:**

1. Software accesses $C0EC (Q6L) in read mode
2. Current nibble at head position is returned in data latch
3. Position advances on each read (simulating continuous disk rotation)
4. Nibbles wrap at track boundary (6656 nibbles per track)

#### Video Display

The video system renders 40-column text mode (280x192 pixels) using:

- Character ROM (341-0160-A) for glyph rendering
- Green phosphor colors matching classic Apple II monitors
- Support for normal, inverse, and flashing characters
- Metal texture for hardware-accelerated display
- Aspect-ratio-preserving scaling

The video reads directly from main RAM, bypassing MMU soft switches, matching how real Apple IIe hardware works.

### Technical Implementation Details

#### 6-and-2 Disk Encoding

The emulator uses authentic Apple II 6-and-2 encoding to convert .dsk sector data into nibbles:

**Why 6-and-2 Encoding?**
- Disk nibbles must have bit 7 set (values $80-$FF) to be valid
- This ensures self-clocking for the drive electronics
- 6-and-2 encoding maps 8-bit bytes to valid nibble values

**Encoding Process (256 bytes → 342 nibbles):**

1. **Split Phase** - Each 256-byte sector is split into:
   - 86 6-bit values (lower 6 bits of each byte, plus padding)
   - 256 2-bit values (upper 2 bits of each byte)

2. **Translation** - 6-bit values are translated using a lookup table to valid nibbles ($96-$FF)
   - Example: 6-bit `0x00` → nibble `0x96`
   - Ensures all nibbles have bit 7 set

3. **XOR Encoding** - Data is XOR-encoded for error detection
   - Each nibble is XORed with the previous nibble
   - Creates a checksum chain

4. **Sector Structure** - Each sector becomes:
   ```
   Address Field:
   - D5 AA 96           (address prologue)
   - Volume (4-and-4)   (2 nibbles)
   - Track (4-and-4)    (2 nibbles)
   - Sector (4-and-4)   (2 nibbles)
   - Checksum (4-and-4) (2 nibbles)
   - DE AA EB           (address epilogue)

   Gap: FF FF FF FF FF  (self-sync bytes)

   Data Field:
   - D5 AA AD           (data prologue)
   - 342 nibbles        (6-and-2 encoded data)
   - Checksum nibble
   - DE AA EB           (data epilogue)

   Gap: FF ... FF       (self-sync bytes to fill track)
   ```

5. **Track Assembly** - 16 sectors arranged with gaps to total 6656 nibbles per track

**Decoding (for future write support):**
- Reverse the XOR chain
- Translate nibbles back to 6-bit values
- Recombine 6-bit and 2-bit portions into 8-bit bytes

#### Device Bus Architecture

All peripherals (Keyboard, Speaker, Disk II) implement the `Device` interface:

```cpp
class Device {
    virtual uint8_t read(uint16_t address) = 0;
    virtual void write(uint16_t address, uint8_t value) = 0;
    virtual AddressRange getAddressRange() const = 0;
};
```

The MMU routes addresses to devices:
- Devices register their address ranges
- MMU checks device ranges before accessing RAM/ROM
- Multiple devices can coexist (keyboard at $C000, disk at $C0E0)

#### Memory Banking Implementation

The MMU implements Apple IIe's complex banking:

**80STORE Mode:**
- When enabled, PAGE2 switch affects RAM banking instead of video page
- Text/Lo-res Page 1: $0400-$07FF always uses main RAM
- Text/Lo-res Page 2: $0800-$0BFF uses main or aux based on PAGE2

**RAMRD/RAMWRT:**
- Independent control of read and write banking
- Allows reading from one bank while writing to another
- Used for fast memory-to-memory copies between banks

**Language Card:**
- Pre-write protection: requires two successive writes to same address
- Prevents accidental ROM overwrites
- Three banks: D0-DF (12KB), E0-FF (8KB), second D0-DF bank
- Read and write enable controlled independently

### Data Flow

```
Keyboard Input → Video Window → Keyboard Device (latch) → MMU → CPU reads $C000
                                                                      ↓
CPU executes → MMU routes → RAM/ROM/Devices ← CPU fetches instructions
                  ↓                ↓
            Soft Switches    Disk II Controller ($C0E0-$C0EF)
                  ↓                ↓
         Update state      Stepper Motor + Read Head
                                   ↓
                           Disk Image (nibbles)
                                   ↓
Video Window reads RAM → Character ROM lookup → Metal Texture → Display

Speaker Toggle ($C030) → Audio Buffer → SDL Audio Output
```

### Component Diagram

```
emulator
├── CPU (65C02 via MOS6502 library)
│   ├── Read Callback → MMU::read()
│   └── Write Callback → MMU::write()
├── MMU (Memory Management Unit)
│   ├── RAM (64KB main + 64KB aux)
│   ├── ROM (12KB system + slot ROMs)
│   ├── Device routing
│   │   ├── Keyboard ($C000, $C010)
│   │   ├── Speaker ($C030)
│   │   └── Disk II ($C0E0-$C0EF, slot 6)
│   └── Soft Switch State
├── Disk II Controller
│   ├── Drive 1 State (track, position, disk image)
│   ├── Drive 2 State (track, position, disk image)
│   ├── Stepper Motor Logic
│   └── Read/Write Logic
├── Disk Images
│   ├── DSK file loader
│   ├── 6-and-2 nibble encoder
│   └── Track nibble storage (35 tracks × 6656 nibbles)
├── Speaker
│   ├── Toggle detection
│   ├── Audio buffer
│   └── SDL audio output
├── window_manager
│   ├── SDL3 Window
│   ├── Metal Device/Queue
│   └── ImGui Context
├── UI Windows
│   ├── video_window (Apple IIe display with keyboard input)
│   ├── cpu_window (CPU register viewer)
│   ├── memory_viewer_window (memory hex dump)
│   ├── soft_switches_window (soft switch states)
│   └── disk_window (disk activity and load/eject)
└── preferences (persistent settings)
```

## User Interface

### Windows

- **Video Display** - Main Apple IIe screen output, accepts keyboard input when focused
- **CPU Registers** - Shows PC, SP, A, X, Y, and status flags in real-time
- **Memory Viewer** - Hex dump view of memory with navigation controls
- **Soft Switches** - Live display of all soft switch states (memory, video, language card)
- **Disk Activity** - Shows current disk status with load/eject buttons for both drives
  - Drive number, track position, motor status, selected drive
  - Read head position within track
  - Load .dsk files with file browser
  - Eject disks from drives

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
│   ├── emulator/             # Core emulator components
│   │   ├── bus.hpp           # Device bus interface
│   │   ├── device.hpp        # Device interface (keyboard, disk, etc.)
│   │   ├── disk2.hpp         # Disk II controller emulation
│   │   ├── disk_image.hpp    # Disk image loading and nibble encoding
│   │   ├── emulator.hpp      # Main emulator class
│   │   ├── keyboard.hpp      # Keyboard device with latch
│   │   ├── mmu.hpp           # Memory Management Unit
│   │   ├── ram.hpp           # RAM storage
│   │   ├── rom.hpp           # ROM loading
│   │   ├── speaker.hpp       # Speaker toggle and audio
│   │   └── video_display.hpp # Video rendering logic
│   ├── ui/                   # User interface windows
│   │   ├── base_window.hpp   # Base window class
│   │   ├── cpu_window.hpp    # CPU register display
│   │   ├── disk_window.hpp   # Disk activity and controls
│   │   ├── memory_viewer_window.hpp # Memory hex viewer
│   │   ├── soft_switches_window.hpp # Soft switch status
│   │   └── window_manager.hpp # Window management
│   ├── application.hpp       # Main application
│   └── preferences.hpp       # Settings persistence
├── src/
│   ├── emulator/             # Emulator implementations
│   │   ├── bus.cpp
│   │   ├── disk2.cpp         # Disk II controller logic
│   │   ├── disk_image.cpp    # DSK loading & 6-and-2 encoding
│   │   ├── emulator.cpp      # Main emulation loop
│   │   ├── keyboard.cpp
│   │   ├── mmu.cpp           # MMU routing and soft switches
│   │   ├── ram.cpp
│   │   ├── rom.cpp
│   │   └── speaker.cpp       # Audio generation
│   ├── ui/                   # UI implementations
│   │   ├── cpu_window.cpp
│   │   ├── disk_window.cpp
│   │   ├── memory_viewer_window.cpp
│   │   ├── soft_switches_window.cpp
│   │   └── window_manager.cpp
│   ├── application.cpp
│   ├── main.cpp
│   └── preferences.cpp
├── include/roms/             # ROM files (not in repo)
│   ├── CD_ROM.bin
│   ├── EF_ROM.bin
│   ├── disk/
│   │   └── P5_DISK2.bin
│   └── video/
│       └── 341-0160-A.bin
├── tests/                    # Test programs
└── CMakeLists.txt
```

## Current Status

### Implemented

- Complete 65C02 CPU emulation with cycle counting
- Full Apple IIe memory map with bank switching (128KB total)
- All IIe soft switches for memory management
- Language card emulation with pre-write protection
- 40-column text mode with character ROM
- Keyboard input with latch model and key repeat
- **Disk II controller in slot 6 (read-only)**
- **DSK disk image support with 6-and-2 nibble encoding**
- **Disk activity window with load/eject controls**
- **Speaker audio output**
- Soft switches status window
- Metal-accelerated video display
- 50Hz frame timing
- ImGui-based UI with docking
- Persistent window state

### In Progress / Planned

- Disk write support (currently read-only)
- Graphics modes (Lo-res, Hi-res, Double Hi-res)
- 80-column text mode
- NIB disk image format support
- Copy protection bypass for protected disks
- Mockingboard audio card
- Debugger with breakpoints and single-stepping
- Disassembler window
- Save states
- Cross-platform rendering (currently macOS Metal only)

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

## Known Limitations

### Current Restrictions

- **Disk Write Not Implemented** - Disks are read-only; cannot save files to disk images
- **Text Mode Only** - Graphics modes (Lo-res, Hi-res) not yet implemented
- **40-Column Only** - 80-column text mode not available
- **macOS Only** - Metal rendering currently requires macOS
- **No Copy Protection Support** - Protected disks that rely on timing or non-standard formats won't work

### Disk Compatibility

**Works:**
- Standard DOS 3.3 disks (.dsk format)
- Most games that use standard disk formats (Arkanoid 2, etc.)
- BASIC programs
- Most educational and productivity software

**Doesn't Work Yet:**
- Copy-protected games (timing-sensitive protection schemes)
- Nibble-format (.nib) disk images
- Writing to disks (read-only currently)
- Protected disks requiring non-standard track formats

### Performance

The emulator runs at accurate Apple IIe speed (1.023 MHz) with 50Hz display refresh. This provides authentic timing for most software, though some timing-critical copy protection may not work without cycle-accurate disk timing.

## Future Enhancements

- Cycle-accurate disk timing for copy protection
- Graphics mode support (Lo-res, Hi-res, Double Hi-res)
- 80-column text mode
- Disk write support
- NIB disk image format
- Save states
- Integrated debugger with breakpoints
- Disassembler window
- Mockingboard sound card
- Cross-platform rendering (OpenGL/Vulkan)
- Joystick support
- Printer emulation

## License

TBD
