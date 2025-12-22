cmake_minimum_required(VERSION 3.20)
project(a2e VERSION 0.1.0 LANGUAGES CXX)

# macOS: Set SDK path automatically if not specified
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler flags based on build type
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -march=native)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Zi /Od)
    else()
        add_compile_options(/O2)
    endif()
endif()

# Add MOS6502 submodule
add_subdirectory(external/MOS6502)

# Add SDL3 submodule (build static library)
set(SDL_STATIC ON CACHE BOOL "Build a static version of the library")
add_subdirectory(external/SDL3)

# Find Metal framework (macOS only)
if(APPLE)
    find_library(METAL_FRAMEWORK Metal)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
endif()

# Find PortAudio
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# IMGUI library
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    # Use custom Metal backend with sampler control
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_backend/imgui_impl_metal_custom.mm
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_backend
)

target_link_libraries(imgui PUBLIC
    SDL3::Headers
    SDL3::SDL3-static
)

if(APPLE)
    target_link_libraries(imgui PUBLIC
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
    )
endif()

# Ensure SDL3 is built before imgui
add_dependencies(imgui SDL3-static)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/MOS6502/include
    ${PORTAUDIO_INCLUDE_DIRS}
)

# Main executable
add_executable(a2e
    src/main.cpp
    src/application.cpp
    src/preferences.cpp
    # UI components
    src/ui/window_renderer.mm
    src/ui/window_manager.cpp
    src/ui/cpu_window.cpp
    src/ui/memory_viewer_window.cpp
    src/ui/video_window.mm
    src/ui/soft_switches_window.cpp
    # Utilities
    src/utils/resource_path.mm
    src/utils/paste_handler.cpp
    # Emulator components
    src/emulator/bus.cpp
    src/emulator/ram.cpp
    src/emulator/rom.cpp
    src/emulator/mmu.cpp
    src/emulator/keyboard.cpp
    src/emulator/speaker.cpp
    src/emulator/video_display.mm
    src/emulator/emulator.cpp
)

# Link libraries
target_link_libraries(a2e PRIVATE
    MOS6502
    imgui
    SDL3::SDL3-static
    ${PORTAUDIO_LIBRARIES}
)

target_link_directories(a2e PRIVATE ${PORTAUDIO_LIBRARY_DIRS})

if(APPLE)
    target_link_libraries(a2e PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        "-framework Foundation"
    )
endif()

# Set output directories
set_target_properties(a2e PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy ROM files to the build directory
add_custom_command(TARGET a2e POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:a2e>/../resources/roms/system/
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:a2e>/../resources/roms/character/
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:a2e>/../resources/roms/peripheral/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0134-A-EF.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0135-A-CD.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0349-B-C0-FF.bin
        $<TARGET_FILE_DIR:a2e>/../resources/roms/system/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/roms/character/341-0160-A.bin
        $<TARGET_FILE_DIR:a2e>/../resources/roms/character/
    COMMENT "Copying ROM files to build directory"
)

# Copy Disk II ROM if it exists (separate command to avoid build failure if missing)
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/roms/peripheral/341-0027.bin")
    add_custom_command(TARGET a2e POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/roms/peripheral/341-0027.bin
            $<TARGET_FILE_DIR:a2e>/../resources/roms/peripheral/
        COMMENT "Copying Disk II ROM to build directory"
    )
endif()

# ROM Execution Test
add_executable(rom_execution_test
    tests/rom_execution_test.cpp
    # Utilities
    src/utils/resource_path.mm
    # Emulator components (without GUI)
    src/emulator/ram.cpp
    src/emulator/rom.cpp
    src/emulator/mmu.cpp
    src/emulator/keyboard.cpp
    src/emulator/speaker.cpp
)

target_link_libraries(rom_execution_test PRIVATE
    MOS6502
    SDL3::SDL3-static
)

if(APPLE)
    target_link_libraries(rom_execution_test PRIVATE
        "-framework Foundation"
    )
endif()

set_target_properties(rom_execution_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy ROM files for the test
add_custom_command(TARGET rom_execution_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:rom_execution_test>/../resources/roms/system/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0134-A-EF.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0135-A-CD.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0349-B-C0-FF.bin
        $<TARGET_FILE_DIR:rom_execution_test>/../resources/roms/system/
    COMMENT "Copying ROM files for test"
)

# Keyboard I/O Test
add_executable(keyboard_io_test
    tests/keyboard_io_test.cpp
    src/emulator/keyboard.cpp
)

target_link_libraries(keyboard_io_test PRIVATE
    MOS6502
)

set_target_properties(keyboard_io_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# PC Stuck Diagnostic Test
add_executable(pc_stuck_diagnostic
    tests/pc_stuck_diagnostic.cpp
    src/utils/resource_path.mm
    src/emulator/ram.cpp
    src/emulator/rom.cpp
    src/emulator/mmu.cpp
    src/emulator/keyboard.cpp
    src/emulator/speaker.cpp
)

target_link_libraries(pc_stuck_diagnostic PRIVATE
    MOS6502
    SDL3::SDL3-static
)

if(APPLE)
    target_link_libraries(pc_stuck_diagnostic PRIVATE
        "-framework Foundation"
    )
endif()

set_target_properties(pc_stuck_diagnostic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET pc_stuck_diagnostic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:pc_stuck_diagnostic>/../resources/roms/system/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0134-A-EF.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0135-A-CD.bin
        ${CMAKE_SOURCE_DIR}/resources/roms/system/342-0349-B-C0-FF.bin
        $<TARGET_FILE_DIR:pc_stuck_diagnostic>/../resources/roms/system/
    COMMENT "Copying ROM files for diagnostic test"
)
