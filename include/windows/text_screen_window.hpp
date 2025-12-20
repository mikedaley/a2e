#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <functional>
#include <array>

/**
 * Text Screen Window
 *
 * Renders the Apple IIe 40-column text screen.
 * Displays the contents of text page 1 ($0400-$07FF) as characters.
 */
class text_screen_window : public base_window
{
public:
  /**
   * Constructor
   */
  text_screen_window();

  /**
   * Destructor
   */
  ~text_screen_window() override = default;

  /**
   * Render the text screen window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Text Screen"; }

  /**
   * Set the memory read callback
   * @param callback Function that reads a byte from an address
   */
  void setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

  /**
   * Set which text page to display
   * @param page 1 for page 1 ($0400), 2 for page 2 ($0800)
   */
  void setTextPage(int page);

  /**
   * Load character ROM data for rendering
   * @param data Pointer to 2KB character ROM data
   * @return true on success
   */
  bool loadCharacterROM(const uint8_t *data, size_t size);

private:
  /**
   * Render the text display area
   */
  void renderTextDisplay();

  /**
   * Convert Apple IIe character code to displayable character
   * @param code Raw character code from memory
   * @return ASCII character to display
   */
  char convertCharacter(uint8_t code) const;

  /**
   * Check if character should be displayed as inverse
   * @param code Raw character code
   * @return true if inverse video
   */
  bool isInverse(uint8_t code) const;

  /**
   * Check if character should be flashing
   * @param code Raw character code
   * @return true if flashing
   */
  bool isFlashing(uint8_t code) const;

  std::function<uint8_t(uint16_t)> memory_read_callback_;
  int text_page_ = 1;
  
  // Flash state
  bool flash_state_ = false;
  int flash_counter_ = 0;
  static constexpr int FLASH_RATE = 30; // frames between flash toggles

  // Text screen dimensions
  static constexpr int TEXT_WIDTH = 40;
  static constexpr int TEXT_HEIGHT = 24;

  // Text page base addresses
  static constexpr uint16_t TEXT_PAGE1_BASE = 0x0400;
  static constexpr uint16_t TEXT_PAGE2_BASE = 0x0800;

  // Apple IIe text screen row offsets (non-linear memory layout)
  static constexpr uint16_t ROW_OFFSETS[24] = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0
  };
};
