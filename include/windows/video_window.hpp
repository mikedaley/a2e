#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <functional>
#include <array>
#include <vector>

/**
 * Video Window
 *
 * Renders the Apple IIe video display as a texture in an ImGui window.
 * Supports 40-column text mode with proper character ROM rendering.
 */
class video_window : public base_window
{
public:
  /**
   * Constructor
   */
  video_window();

  /**
   * Destructor
   */
  ~video_window() override;

  /**
   * Render the video window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Video Display"; }

  /**
   * Set the memory read callback
   * @param callback Function that reads a byte from an address
   */
  void setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

  /**
   * Set the key press callback
   * @param callback Function that handles a key press (Apple IIe key code)
   */
  void setKeyPressCallback(std::function<void(uint8_t)> callback);

  /**
   * Load character ROM for text rendering
   * @param filepath Path to the character ROM file
   * @return true on success
   */
  bool loadCharacterROM(const std::string &filepath);

  /**
   * Update the video buffer from memory
   * Call this each frame to refresh the display
   */
  void updateDisplay();

  /**
   * Initialize the Metal texture for rendering
   * @param device Metal device pointer (id<MTLDevice>)
   * @return true on success
   */
  bool initializeTexture(void *device);

private:
  /**
   * Render text mode (40x24)
   */
  void renderTextMode();

  /**
   * Handle keyboard input when window has focus
   */
  void handleKeyboardInput();

  /**
   * Convert ImGui key code to Apple IIe key code
   * @param key ImGui key code
   * @param shift true if shift is held
   * @param ctrl true if control is held
   * @return Apple IIe key code (0-127), or 0xFF if not mappable
   */
  uint8_t convertKeyCode(int key, bool shift, bool ctrl);

  /**
   * Draw a character at the specified position
   * @param col Column (0-39)
   * @param row Row (0-23)
   * @param ch Character code from memory
   */
  void drawCharacter(int col, int row, uint8_t ch);

  /**
   * Set a pixel in the frame buffer
   * @param x X coordinate
   * @param y Y coordinate
   * @param color RGBA color value
   */
  void setPixel(int x, int y, uint32_t color);

  /**
   * Upload frame buffer to texture
   */
  void uploadTexture();

  std::function<uint8_t(uint16_t)> memory_read_callback_;
  std::function<void(uint8_t)> key_press_callback_;

  // Character ROM (256 characters, 8 bytes each = 2KB)
  static constexpr size_t CHAR_ROM_SIZE = 2048;
  std::array<uint8_t, CHAR_ROM_SIZE> char_rom_;
  bool char_rom_loaded_ = false;

  // Display dimensions
  static constexpr int TEXT_WIDTH = 40;
  static constexpr int TEXT_HEIGHT = 24;
  static constexpr int CHAR_WIDTH = 7;
  static constexpr int CHAR_HEIGHT = 8;
  static constexpr int DISPLAY_WIDTH = TEXT_WIDTH * CHAR_WIDTH;   // 280 pixels
  static constexpr int DISPLAY_HEIGHT = TEXT_HEIGHT * CHAR_HEIGHT; // 192 pixels

  // Frame buffer (RGBA)
  std::vector<uint32_t> frame_buffer_;

  // Metal texture handle
  void *texture_ = nullptr;  // id<MTLTexture>
  void *device_ = nullptr;   // id<MTLDevice>
  bool texture_initialized_ = false;

  // Flash state for flashing characters
  bool flash_state_ = false;
  int flash_counter_ = 0;
  static constexpr int FLASH_RATE = 30;

  // Refresh rate limiting
  int frame_skip_counter_ = 0;
  static constexpr int FRAME_SKIP = 2;  // Update every N frames (reduce GPU load)
  
  // Cache previous text page to detect changes
  std::array<uint8_t, TEXT_WIDTH * TEXT_HEIGHT> prev_text_page_;
  bool prev_flash_state_ = false;
  bool needs_redraw_ = true;

  // Text page base addresses
  static constexpr uint16_t TEXT_PAGE1_BASE = 0x0400;
  static constexpr uint16_t TEXT_PAGE2_BASE = 0x0800;

  // Apple IIe text screen row offsets (non-linear memory layout)
  static constexpr uint16_t ROW_OFFSETS[24] = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0
  };

  // Colors (green phosphor monitor style)
  static constexpr uint32_t COLOR_GREEN = 0xFF00FF00;  // ABGR format for Metal
  static constexpr uint32_t COLOR_BLACK = 0xFF000000;

  // Key repeat support
  int held_key_ = -1;              // Currently held key (ImGuiKey), -1 if none
  uint8_t held_apple_key_ = 0;     // Apple IIe keycode for held key
  float repeat_timer_ = 0.0f;      // Timer for repeat
  bool repeat_started_ = false;    // Has initial delay passed?
  
  // Apple IIe key repeat timing (approximately 10 repeats/second after 0.5s delay)
  static constexpr float REPEAT_DELAY = 0.5f;    // Initial delay before repeat starts
  static constexpr float REPEAT_RATE = 0.1f;     // Time between repeats (10/second)
};
