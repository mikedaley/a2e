#pragma once

#include "apple2e/soft_switches.hpp"
#include <cstdint>
#include <functional>
#include <array>
#include <vector>
#include <string>

/**
 * Video standard selection for display output
 */
enum class VideoStandard : uint8_t
{
  NTSC = 0,      // NTSC artifact colors (American standard)
  PAL_COLOR = 1, // PAL Apple IIe style (TCA650 encoder, different colors)
  PAL_MONO = 2   // PAL monochrome (what most European users saw)
};

/**
 * Video Display
 *
 * Generates the Apple IIe video output as a texture.
 * Handles all video modes: text (40/80-column), lo-res, and hi-res graphics.
 * This is an emulator component - the actual display is handled by the UI layer.
 */
class video_display
{
public:
  /**
   * Constructor
   */
  video_display();

  /**
   * Destructor
   */
  ~video_display();

  /**
   * Initialize the Metal texture for rendering
   * @param device Metal device pointer (id<MTLDevice>)
   * @return true on success
   */
  bool initializeTexture(void *device);

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
  void update();

  /**
   * Set the memory read callback (main RAM)
   * @param callback Function that reads a byte from an address
   */
  void setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

  /**
   * Set the auxiliary memory read callback for 80-column mode
   * @param callback Function that reads a byte from aux memory at an address
   */
  void setAuxMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

  /**
   * Set the video mode state callback
   * @param callback Function that returns current soft switch state
   */
  void setVideoModeCallback(std::function<Apple2e::SoftSwitchState()> callback);

  /**
   * Get the texture handle for rendering
   * @return Metal texture pointer (id<MTLTexture>), or nullptr if not initialized
   */
  void *getTexture() const { return texture_; }

  /**
   * Check if texture is initialized
   * @return true if texture is ready for use
   */
  bool isTextureInitialized() const { return texture_initialized_; }

  /**
   * Get the current display width (changes based on 40/80 column mode)
   * @return Current display width in pixels
   */
  int getCurrentDisplayWidth() const { return current_display_width_; }

  /**
   * Get the maximum display width (80-column mode)
   * @return Maximum display width in pixels
   */
  static constexpr int getMaxDisplayWidth() { return DISPLAY_WIDTH; }

  /**
   * Get the display height
   * @return Display height in pixels
   */
  static constexpr int getDisplayHeight() { return DISPLAY_HEIGHT; }

  /**
   * Get the 40-column display width
   * @return 40-column display width in pixels
   */
  static constexpr int getDisplayWidth40() { return DISPLAY_WIDTH_40; }

  /**
   * Enable or disable color fringing (artifact colors blending to white)
   * When enabled, adjacent hi-res pixels blend to white (authentic NTSC look)
   * When disabled, pixels show their true artifact color (cleaner look)
   * Only applies to NTSC mode.
   * @param enabled true to enable fringing, false to disable
   */
  void setColorFringing(bool enabled) { color_fringing_enabled_ = enabled; }

  /**
   * Check if color fringing is enabled
   * @return true if color fringing is enabled
   */
  bool isColorFringingEnabled() const { return color_fringing_enabled_; }

  /**
   * Set the video standard (NTSC, PAL Color, or PAL Mono)
   * @param standard The video standard to use
   */
  void setVideoStandard(VideoStandard standard) { video_standard_ = standard; }

  /**
   * Get the current video standard
   * @return Current video standard
   */
  VideoStandard getVideoStandard() const { return video_standard_; }

  /**
   * Set whether to use green phosphor text (true) or white text (false)
   * @param green true for green phosphor, false for white
   */
  void setGreenText(bool green) { green_text_ = green; }

  /**
   * Check if green phosphor text is enabled
   * @return true if green text, false if white
   */
  bool isGreenText() const { return green_text_; }

private:
  /**
   * Render text mode (40 or 80 column based on col80_mode)
   */
  void renderTextMode();

  /**
   * Render 40-column text mode (40x24)
   */
  void renderTextMode40();

  /**
   * Render 80-column text mode (80x24)
   * Uses interleaved memory: even columns from aux RAM, odd columns from main RAM
   */
  void renderTextMode80();

  /**
   * Render hi-res graphics mode (280x192)
   */
  void renderHiResMode();

  /**
   * Render lo-res graphics mode (40x48)
   */
  void renderLoResMode();

  /**
   * Get the hi-res color for a pixel based on bit pattern and position
   * @param bit_on Whether the pixel bit is on
   * @param x_pos X position of pixel (for odd/even determination)
   * @param high_bit The high bit (bit 7) of the byte - selects color palette
   * @param prev_bit Previous pixel's bit state (for color blending)
   * @param next_bit Next pixel's bit state (for color blending)
   * @return RGBA color value
   */
  uint32_t getHiResColor(bool bit_on, int x_pos, bool high_bit, bool prev_bit, bool next_bit);

  /**
   * Draw a character at the specified position (40-column mode)
   * @param col Column (0-39)
   * @param row Row (0-23)
   * @param ch Character code from memory
   */
  void drawCharacter(int col, int row, uint8_t ch);

  /**
   * Draw a character at the specified position (80-column mode)
   * @param col Column (0-79)
   * @param row Row (0-23)
   * @param ch Character code from memory
   */
  void drawCharacter80(int col, int row, uint8_t ch);

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

  // Callbacks
  std::function<uint8_t(uint16_t)> memory_read_callback_;
  std::function<uint8_t(uint16_t)> aux_memory_read_callback_;
  std::function<Apple2e::SoftSwitchState()> video_mode_callback_;

  // Character ROM (256 characters, 8 bytes each = 2KB per set, 2 sets = 4KB)
  // Primary set at offset 0, alternate set at offset 2048
  static constexpr size_t CHAR_SET_SIZE = 2048;       // Size of each character set
  static constexpr size_t CHAR_ROM_SIZE = 4096;       // Total: primary + alternate
  static constexpr size_t CHAR_ROM_ALT_OFFSET = 2048; // Offset to alternate set
  std::array<uint8_t, CHAR_ROM_SIZE> char_rom_;
  bool char_rom_loaded_ = false;

  // Display dimensions
  static constexpr int TEXT_WIDTH_40 = 40;
  static constexpr int TEXT_WIDTH_80 = 80;
  static constexpr int TEXT_HEIGHT = 24;
  static constexpr int CHAR_WIDTH = 7;
  static constexpr int CHAR_HEIGHT = 8;
  static constexpr int DISPLAY_WIDTH_40 = TEXT_WIDTH_40 * CHAR_WIDTH; // 280 pixels
  static constexpr int DISPLAY_WIDTH_80 = TEXT_WIDTH_80 * CHAR_WIDTH; // 560 pixels
  static constexpr int DISPLAY_HEIGHT = TEXT_HEIGHT * CHAR_HEIGHT;    // 192 pixels

  // Use 80-column width as the maximum display width
  static constexpr int DISPLAY_WIDTH = DISPLAY_WIDTH_80;

  // Legacy alias for compatibility
  static constexpr int TEXT_WIDTH = TEXT_WIDTH_40;

  // Frame buffer (RGBA)
  std::vector<uint32_t> frame_buffer_;

  // Metal texture handle
  void *texture_ = nullptr; // id<MTLTexture>
  void *device_ = nullptr;  // id<MTLDevice>
  bool texture_initialized_ = false;

  // Color fringing: true = adjacent pixels blend to white (authentic), false = pure artifact colors
  // Only applies to NTSC mode
  bool color_fringing_enabled_ = true;

  // Video standard selection (NTSC, PAL Color, PAL Mono)
  VideoStandard video_standard_ = VideoStandard::NTSC;

  // Text color: true = green phosphor, false = white
  bool green_text_ = true;

  // Current display width (changes based on 40/80 column mode)
  int current_display_width_ = DISPLAY_WIDTH_40;

  // Effective 80-column mode (accounts for CSW vector state)
  bool effective_col80_mode_ = false;

  // Previous 80-column mode state (to detect mode changes)
  bool prev_col80_mode_ = false;

  // Flash state for flashing characters
  bool flash_state_ = false;
  int flash_counter_ = 0;
  static constexpr int FLASH_RATE = 30;

  // Text page base addresses
  static constexpr uint16_t TEXT_PAGE1_BASE = 0x0400;
  static constexpr uint16_t TEXT_PAGE2_BASE = 0x0800;

  // Hi-res page base addresses
  static constexpr uint16_t HIRES_PAGE1_BASE = 0x2000;
  static constexpr uint16_t HIRES_PAGE2_BASE = 0x4000;

  // Apple IIe text screen row offsets (non-linear memory layout)
  static constexpr uint16_t ROW_OFFSETS[24] = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

  // Colors (ABGR format for Metal)
  static constexpr uint32_t COLOR_BLACK = 0xFF000000;
  static constexpr uint32_t COLOR_WHITE = 0xFFFFFFFF;

  // Monochrome green phosphor
  static constexpr uint32_t COLOR_GREEN = 0xFF00FF00;

  // Hi-res NTSC artifact colors (Group 1: high bit = 0)
  static constexpr uint32_t COLOR_PURPLE = 0xFFFF00FF;      // Violet/Purple
  static constexpr uint32_t COLOR_GREEN_HIRES = 0xFF00FF00; // Green

  // Hi-res NTSC artifact colors (Group 2: high bit = 1)
  static constexpr uint32_t COLOR_BLUE = 0xFFFF8000;   // Blue (in ABGR)
  static constexpr uint32_t COLOR_ORANGE = 0xFF0080FF; // Orange (in ABGR)

  // Hi-res PAL artifact colors (TCA650 encoder - colors are shifted from NTSC)
  // The PAL encoder produced noticeably different colors due to the different
  // color subcarrier frequency (4.43 MHz vs 3.58 MHz) and encoding scheme.
  // Colors also only change every 2 pixels instead of every pixel.
  // Group 1 (high bit = 0): Cyan-ish / Red-ish
  static constexpr uint32_t COLOR_PAL_CYAN = 0xFFFFFF00;    // Cyan (in ABGR)
  static constexpr uint32_t COLOR_PAL_RED = 0xFF0000FF;     // Red (in ABGR)
  // Group 2 (high bit = 1): Yellow-ish / Blue-ish
  static constexpr uint32_t COLOR_PAL_YELLOW = 0xFF00FFFF;  // Yellow (in ABGR)
  static constexpr uint32_t COLOR_PAL_BLUE = 0xFFFF0000;    // Blue (in ABGR)

  // Lo-res colors (16 colors) - NTSC
  static constexpr uint32_t LORES_COLORS[16] = {
      0xFF000000, // 0: Black
      0xFF0000E0, // 1: Magenta (Dark Red)
      0xFF000080, // 2: Dark Blue
      0xFF8000FF, // 3: Purple (Violet)
      0xFF008000, // 4: Dark Green
      0xFF808080, // 5: Grey 1
      0xFFFF0000, // 6: Medium Blue
      0xFFFF80C0, // 7: Light Blue
      0xFF004080, // 8: Brown
      0xFF0080FF, // 9: Orange
      0xFF808080, // 10: Grey 2
      0xFF80C0FF, // 11: Pink
      0xFF00FF00, // 12: Light Green (Green)
      0xFF00FFFF, // 13: Yellow
      0xFFFFFF00, // 14: Aqua
      0xFFFFFFFF  // 15: White
  };

  // Lo-res colors for PAL (TCA650 encoder) - shifted from NTSC
  // The PAL encoder produced different hues due to the color subcarrier difference
  static constexpr uint32_t LORES_COLORS_PAL[16] = {
      0xFF000000, // 0: Black
      0xFF0000C0, // 1: Red (shifted from Magenta)
      0xFF800000, // 2: Blue (shifted)
      0xFFFF0080, // 3: Magenta (shifted from Purple)
      0xFF008000, // 4: Dark Green
      0xFF808080, // 5: Grey 1
      0xFFFF8000, // 6: Cyan (shifted from Medium Blue)
      0xFFFFFF80, // 7: Light Cyan
      0xFF004080, // 8: Brown
      0xFF00FFFF, // 9: Yellow (shifted from Orange)
      0xFF808080, // 10: Grey 2
      0xFF80FFFF, // 11: Light Yellow
      0xFF00FF00, // 12: Light Green
      0xFF80FF80, // 13: Light Green-Yellow
      0xFFFFFF00, // 14: Cyan
      0xFFFFFFFF  // 15: White
  };

  // Lo-res grayscale for PAL Mono mode
  static constexpr uint32_t LORES_COLORS_MONO[16] = {
      0xFF000000, // 0: Black
      0xFF303030, // 1
      0xFF404040, // 2
      0xFF505050, // 3
      0xFF606060, // 4
      0xFF808080, // 5: Grey
      0xFF707070, // 6
      0xFF909090, // 7
      0xFF505050, // 8
      0xFFA0A0A0, // 9
      0xFF808080, // 10: Grey
      0xFFB0B0B0, // 11
      0xFFC0C0C0, // 12
      0xFFD0D0D0, // 13
      0xFFE0E0E0, // 14
      0xFFFFFFFF  // 15: White
  };
};
