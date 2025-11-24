cmake_minimum_required(VERSION 3.20)
project(a2e VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4 /O2)
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

# IMGUI library
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
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
)

# Main executable
add_executable(a2e
    src/main.cpp
    src/window/WindowManager.mm
    src/Application.cpp
)

# Link libraries
target_link_libraries(a2e PRIVATE
    MOS6502
    imgui
    SDL3::SDL3-static
)

if(APPLE)
    target_link_libraries(a2e PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
    )
endif()

# Set output directories
set_target_properties(a2e PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
