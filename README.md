# Apple 2e Emulator (a2e)

A modern C++ Apple IIe emulator using the MOS6502 CPU emulator library.

## Features

- 65C02 CPU emulation (using MOS6502 library)
- Modern C++20 implementation
- Cycle-accurate CPU emulation
- IMGUI-based user interface with docking support
- SDL3 with Metal rendering (macOS) for windowing and graphics
- Modular device-based architecture

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

## Architecture Overview

The emulator uses a modular, device-based architecture that mimics the Apple IIe's hardware design. All components communicate through a central bus system, making it easy to add or modify hardware components.

### Core Components

#### 1. Application ([application.hpp](include/application.hpp), [application.cpp](src/application.cpp))

The `application` class is the top-level coordinator that:
- Owns all emulator components (CPU, Bus, RAM, ROM, MMU, Video, Keyboard)
- Manages the UI windows (CPU registers, status, video display)
- Orchestrates the emulation and rendering loop
- Handles preferences and window state persistence

#### 2. CPU ([MOS6502 Library](external/MOS6502))

The 65C02 CPU emulator is wrapped in a `cpu_wrapper` class that:
- Uses the external MOS6502 library configured for CMOS 65C02 variant
- Connects to the bus via read/write callbacks
- Provides access to CPU registers (PC, SP, A, X, Y, P)
- Currently reset but not yet executing instructions in the main loop (TODO)

#### 3. Bus ([bus.hpp](include/bus.hpp), [bus.cpp](src/bus.cpp))

The `Bus` class acts as the central communication hub:
- Routes memory read/write operations to the appropriate device
- Maintains a priority-based device registry (last registered device wins for overlapping ranges)
- Implements the Device interface pattern for modularity
- Returns 0xFF for unmapped addresses

**Device Registration Order:**
1. Keyboard - handles $C000-$C010
2. MMU - handles entire address space with intelligent routing

#### 4. Device Interface ([device.hpp](include/device.hpp))

All hardware components implement the `Device` interface:
```cpp
class Device {
  virtual uint8_t read(uint16_t address) = 0;
  virtual void write(uint16_t address, uint8_t value) = 0;
  virtual AddressRange getAddressRange() const = 0;
  virtual std::string getName() const = 0;
};
```

This enables plug-and-play hardware components.

#### 5. MMU - Memory Management Unit ([mmu.hpp](include/mmu.hpp), [mmu.cpp](src/mmu.cpp))

The `MMU` is the memory router that:
- Handles the Apple IIe memory map ($0000-$FFFF)
- Routes reads/writes to RAM ($0000-$BFFF) or ROM ($D000-$FFFF)
- Manages soft switches ($C000-$C0FF) for video modes and bank switching
- Coordinates keyboard I/O routing
- Tracks system state in `SoftSwitchState`

**Memory Map:**
- $0000-$BFFF: RAM (48KB)
- $C000-$CFFF: I/O and soft switches (4KB)
- $D000-$FFFF: ROM (12KB)

#### 6. RAM ([ram.hpp](include/ram.hpp), [ram.cpp](src/ram.cpp))

The `RAM` device provides:
- 64KB main memory bank
- 64KB auxiliary memory bank
- Separate read/write bank selection
- Direct memory access for MMU and Video

#### 7. ROM ([rom.hpp](include/rom.hpp), [rom.cpp](src/rom.cpp))

The `ROM` device provides:
- 16KB read-only memory
- Initialized to 0xFF (unprogrammed state)
- Can load from file (not yet implemented)
- Silently ignores write attempts

#### 8. Video ([video.hpp](include/video.hpp), [video.cpp](src/video.cpp))

The `Video` device handles:
- Text mode: 40/80 column, 24 lines
- Lo-res graphics: 40×48 pixels
- Hi-res graphics: 280×192 pixels
- Mixed modes (text + graphics)
- SDL3 surface rendering
- Soft switch state updates from MMU

**Video Memory:**
- Text Page 1: $0400-$07FF
- Text Page 2: $0800-$0BFF
- Lo-res Page 1: $0400-$07FF
- Lo-res Page 2: $0800-$0BFF
- Hi-res Page 1: $2000-$3FFF
- Hi-res Page 2: $4000-$5FFF

#### 9. Keyboard ([keyboard.hpp](include/keyboard.hpp), [keyboard.cpp](src/keyboard.cpp))

The `Keyboard` device provides:
- Key queue management
- Strobe flag handling
- Memory-mapped I/O at $C000 (data) and $C010 (strobe clear)
- Key press/release tracking

#### 10. Soft Switches ([soft_switches.hpp](include/apple2e/soft_switches.hpp))

Soft switches control hardware behavior via memory-mapped I/O:
- $C050/$C051: Text/Graphics mode
- $C052/$C053: Full/Mixed screen
- $C054/$C055: Page 1/Page 2
- $C056/$C057: Lo-res/Hi-res graphics
- $C080-$C087: Bank switching

#### 11. Window Renderer ([window_renderer.hpp](include/window/window_renderer.hpp))

The `window_renderer` manages:
- SDL3 window creation and event handling
- Metal rendering on macOS
- IMGUI initialization and frame rendering
- Main event loop with render and update callbacks
- DPI-aware rendering with display scaling
- Live window resizing support

## Main Emulation Loop

The emulation loop is orchestrated by the `application` class through the `window_renderer`:

```cpp
window_renderer::run(renderCallback, updateCallback)
```

### Loop Flow

1. **Event Processing** ([window_renderer.cpp](src/window_renderer.cpp))
   - SDL3 polls for window, keyboard, and mouse events
   - IMGUI processes input events
   - Window close events set `should_close_` flag

2. **Update Phase** ([application.cpp:270](src/application.cpp#L270))
   ```cpp
   void application::update(float deltaTime)
   ```
   - Updates video soft switches from MMU state
   - Renders video frame to SDL surface
   - **TODO:** Execute CPU cycles (not yet implemented)

3. **Render Phase** ([application.cpp:159](src/application.cpp#L159))
   ```cpp
   void application::renderUI()
   ```
   - Renders menu bar (File, View, Help)
   - Creates IMGUI dockspace for window organization
   - Updates CPU window with register values
   - Renders CPU registers window
   - Renders status window
   - Renders video display window

4. **Frame Finalization** ([window_renderer.cpp](src/window_renderer.cpp))
   - IMGUI renders to Metal command buffer
   - Frame is presented to screen
   - VSync waits if enabled

### Data Flow

```
User Input → SDL3 Events → Keyboard Device → MMU → CPU Read
                                                     ↓
CPU Write → Bus → MMU → RAM/ROM/Video/Keyboard ← CPU Fetch
                  ↓
            Soft Switches → Video State
                  ↓
            Video RAM → Video Render → SDL Surface → IMGUI Display
```

### Component Connections

```
application
├── CPU (cpu_wrapper)
│   ├── Read Callback → Bus::read()
│   └── Write Callback → Bus::write()
├── Bus
│   ├── Device: Keyboard ($C000-$C010)
│   └── Device: MMU ($0000-$FFFF)
│       ├── RAM ($0000-$BFFF)
│       ├── ROM ($D000-$FFFF)
│       ├── Keyboard (reference)
│       └── Soft Switches ($C000-$C0FF)
├── Video
│   ├── RAM (reference for video memory)
│   └── Soft Switch State (from MMU)
├── window_renderer
│   ├── SDL3 Window
│   ├── Metal Device/Queue
│   └── IMGUI Context
└── UI Windows
    ├── cpu_window (registers)
    ├── status_window (FPS, timing)
    └── Video Display (Apple IIe screen)
```

## Current Status

### Implemented
✅ Complete device-based architecture
✅ Bus and MMU routing
✅ RAM with dual bank support
✅ ROM device structure
✅ Video subsystem with SDL3
✅ Keyboard input device
✅ Soft switch management
✅ IMGUI-based UI with docking
✅ CPU register visualization
✅ Preferences system with persistence
✅ Metal rendering on macOS

### In Progress
🚧 CPU execution in main loop
🚧 ROM loading from file
🚧 Video texture rendering in IMGUI

### Planned
⏳ Disk I/O emulation
⏳ Audio (speaker/mockingboard)
⏳ Debugger with breakpoints
⏳ Memory viewer
⏳ Disassembler window

## Project Structure

```
a2e/
├── external/
│   ├── MOS6502/          # MOS6502 CPU emulator (submodule)
│   ├── imgui/            # Dear ImGui library (submodule)
│   └── SDL3/             # SDL3 library (submodule)
├── include/              # Public headers
│   ├── apple2e/         # Apple IIe specific definitions
│   │   ├── memory_map.hpp
│   │   └── soft_switches.hpp
│   ├── windows/         # UI window headers
│   │   ├── base_window.hpp
│   │   ├── cpu_window.hpp
│   │   └── status_window.hpp
│   ├── window/          # Window management
│   │   └── window_renderer.hpp
│   ├── application.hpp  # Main application
│   ├── bus.hpp          # Bus system
│   ├── device.hpp       # Device interface
│   ├── mmu.hpp          # Memory management
│   ├── ram.hpp          # RAM device
│   ├── rom.hpp          # ROM device
│   ├── video.hpp        # Video subsystem
│   ├── keyboard.hpp     # Keyboard device
│   └── preferences.hpp  # Settings management
├── src/                 # Implementation files
│   ├── main.cpp        # Entry point
│   ├── application.cpp
│   ├── bus.cpp
│   ├── mmu.cpp
│   ├── ram.cpp
│   ├── rom.cpp
│   ├── video.cpp
│   ├── keyboard.cpp
│   ├── preferences.cpp
│   ├── apple2e/        # Apple IIe implementations
│   └── windows/        # UI window implementations
├── build/              # Build directory (generated)
└── CMakeLists.txt      # CMake configuration
```

## Requirements

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Git (for submodules)
- macOS: Metal framework (included in system)
- Linux: OpenGL development libraries (`libgl1-mesa-dev` or equivalent)
- Windows: OpenGL support (usually included with graphics drivers)

## Development

### Adding a New Device

1. Create a class that implements the `Device` interface
2. Override `read()`, `write()`, `getAddressRange()`, and `getName()`
3. Register the device with the Bus in `application::initialize()`
4. Devices are checked in reverse order (last registered = highest priority)

### Adding a Soft Switch

1. Define the address constant in [soft_switches.hpp](include/apple2e/soft_switches.hpp)
2. Add handling in `MMU::readSoftSwitch()` or `MMU::writeSoftSwitch()`
3. Update `SoftSwitchState` if state tracking is needed
4. Propagate state to affected devices (e.g., Video)

## License

TBD
