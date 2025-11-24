#pragma once

#include <cstdint>

/**
 * Apple IIe Memory Map Constants
 *
 * Defines the memory layout for an unenhanced Apple IIe with 64KB RAM.
 */
namespace Apple2e
{

// Memory regions
constexpr uint16_t MEM_ZERO_PAGE_START = 0x0000;
constexpr uint16_t MEM_ZERO_PAGE_END = 0x00FF;
constexpr uint16_t MEM_STACK_START = 0x0100;
constexpr uint16_t MEM_STACK_END = 0x01FF;

constexpr uint16_t MEM_RAM_START = 0x0200;
constexpr uint16_t MEM_RAM_END = 0xBFFF;

constexpr uint16_t MEM_IO_START = 0xC000;
constexpr uint16_t MEM_IO_END = 0xC0FF;

constexpr uint16_t MEM_EXPANSION_START = 0xC100;
constexpr uint16_t MEM_EXPANSION_END = 0xCFFF;

constexpr uint16_t MEM_ROM_START = 0xD000;
constexpr uint16_t MEM_ROM_END = 0xFFFF;

// Video memory regions
constexpr uint16_t MEM_TEXT_PAGE1_START = 0x0400;
constexpr uint16_t MEM_TEXT_PAGE1_END = 0x07FF;

constexpr uint16_t MEM_TEXT_PAGE2_START = 0x0800;
constexpr uint16_t MEM_TEXT_PAGE2_END = 0x0BFF;

constexpr uint16_t MEM_HIRES_PAGE1_START = 0x2000;
constexpr uint16_t MEM_HIRES_PAGE1_END = 0x3FFF;

constexpr uint16_t MEM_HIRES_PAGE2_START = 0x4000;
constexpr uint16_t MEM_HIRES_PAGE2_END = 0x5FFF;

// Memory sizes
constexpr size_t RAM_SIZE = 0x10000;      // 64KB
constexpr size_t ROM_SIZE = 0x3000;       // 12KB (Monitor + BASIC)
constexpr size_t TEXT_PAGE_SIZE = 0x400;  // 1KB per text page
constexpr size_t HIRES_PAGE_SIZE = 0x2000; // 8KB per hi-res page

} // namespace Apple2e

