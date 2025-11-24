#pragma once

#include <cstdint>

/**
 * Apple IIe Soft Switches
 *
 * Soft switches are memory-mapped I/O locations that control hardware behavior.
 * Reading from these addresses returns status, writing toggles the switch.
 */
namespace Apple2e
{

// Soft switch addresses
constexpr uint16_t SW_TEXT_MODE = 0xC050;
constexpr uint16_t SW_GRAPHICS_MODE = 0xC051;
constexpr uint16_t SW_FULL_SCREEN = 0xC052;
constexpr uint16_t SW_MIXED_MODE = 0xC053;
constexpr uint16_t SW_PAGE1 = 0xC054;
constexpr uint16_t SW_PAGE2 = 0xC055;
constexpr uint16_t SW_LORES = 0xC056;
constexpr uint16_t SW_HIRES = 0xC057;

// Keyboard addresses
constexpr uint16_t KB_DATA = 0xC000;
constexpr uint16_t KB_STROBE = 0xC010;

// Bank switching addresses (for 64KB systems)
constexpr uint16_t BANK_READ_MAIN = 0xC080;
constexpr uint16_t BANK_READ_AUX = 0xC081;
constexpr uint16_t BANK_WRITE_MAIN = 0xC082;
constexpr uint16_t BANK_WRITE_AUX = 0xC083;
constexpr uint16_t BANK_READ_MAIN_WRITE_AUX = 0xC084;
constexpr uint16_t BANK_READ_AUX_WRITE_MAIN = 0xC085;
constexpr uint16_t BANK_READ_MAIN_WRITE_MAIN = 0xC086;
constexpr uint16_t BANK_READ_AUX_WRITE_AUX = 0xC087;

// Soft switch state flags
enum class VideoMode : uint8_t
{
  TEXT = 0,
  GRAPHICS = 1
};

enum class ScreenMode : uint8_t
{
  FULL = 0,
  MIXED = 1
};

enum class PageSelect : uint8_t
{
  PAGE1 = 0,
  PAGE2 = 1
};

enum class GraphicsMode : uint8_t
{
  LORES = 0,
  HIRES = 1
};

enum class MemoryBank : uint8_t
{
  MAIN = 0,
  AUX = 1
};

/**
 * SoftSwitchState - Tracks the current state of all soft switches
 */
struct SoftSwitchState
{
  VideoMode video_mode = VideoMode::TEXT;
  ScreenMode screen_mode = ScreenMode::FULL;
  PageSelect page_select = PageSelect::PAGE1;
  GraphicsMode graphics_mode = GraphicsMode::LORES;
  MemoryBank read_bank = MemoryBank::MAIN;
  MemoryBank write_bank = MemoryBank::MAIN;
  bool keyboard_strobe = false;
};

} // namespace Apple2e

