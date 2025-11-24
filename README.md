# Apple 2e Emulator (a2e)

A modern C++ Apple IIe emulator using the MOS6502 CPU emulator library.

## Features

- 65C02 CPU emulation (using MOS6502 library)
- Modern C++20 implementation
- Cycle-accurate CPU emulation
- IMGUI-based user interface with docking support
- SDL3 for windowing and input handling

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

## Project Structure

```
a2e/
├── external/
│   ├── MOS6502/          # MOS6502 CPU emulator (submodule)
│   ├── imgui/            # Dear ImGui library (submodule)
│   └── SDL3/             # SDL3 library (submodule)
├── include/              # Public headers
├── src/                  # Source files
│   └── main.cpp         # Entry point
├── build/               # Build directory (generated)
└── CMakeLists.txt       # CMake configuration
```

## Requirements

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Git (for submodules)
- OpenGL development libraries
  - macOS: Included in system
  - Linux: `libgl1-mesa-dev` or equivalent
  - Windows: Usually included with graphics drivers

## License

TBD
