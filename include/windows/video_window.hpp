#pragma once

#include "base_window.hpp"
#include "apple2e/soft_switches.hpp"
#include <cstdint>
#include <functional>
#include <array>
#include <vector>

/**
 * Video Window
 *
 * Renders the Apple IIe video display as a texture in an ImGui window.
 * Supports text mode (40/80-column) and hi-res graphics (280x192).
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
   * Set the auxiliary memory read callback for 80-column mode
   * @param callback Function that reads a byte from aux memory at an address
   */
  void setAuxMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

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

  /**
   * Set the video mode state callback
   * @param callback Function that returns current soft switch state
   */
  void setVideoModeCallback(std::function<Apple2e::SoftSwitchState()> callback);

  /**
   * Set the texture filtering mode
   * @param use_linear true for linear filtering, false for nearest neighbor
   */
  void setLinearFiltering(bool use_linear) { use_linear_filtering_ = use_linear; }

  /**
   * Get the current texture filtering mode
   * @return true if using linear filtering, false for nearest neighbor
   */
  bool isLinearFiltering() const { return use_linear_filtering_; }

  /**
   * Enable or disable color fringing (artifact colors blending to white)
   * When enabled, adjacent hi-res pixels blend to white (authentic NTSC look)
   * When disabled, pixels show their true artifact color (cleaner look)
   * @param enabled true to enable fringing, false to disable
   */
  void setColorFringing(bool enabled) { color_fringing_enabled_ = enabled; }

  /**
   * Check if color fringing is enabled
   * @return true if color fringing is enabled
   */
  bool isColorFringingEnabled() const { return color_fringing_enabled_; }

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
   * Handle keyboard input when window has focus
   */
  void handleKeyboardInput();

  /**
   * Convert ImGui key code to Apple IIe key code
   * @param key ImGui key code
   * @param shift true if shift is held
   * @param ctrl true if control is held
   * @param caps_lock true if caps lock is active
   * @return Apple IIe key code (0-127), or 0xFF if not mappable
   */
  uint8_t convertKeyCode(int key, bool shift, bool ctrl, bool caps_lock);

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

  /**
   * Initialize Metal render pipeline for custom texture sampling
   */
  bool initializeRenderPipeline();

  /**
   * Render video texture with custom sampler
   * @param x Screen X position
   * @param y Screen Y position  
   * @param width Display width
   * @param height Display height
   * @param uv_max_x Maximum U coordinate (for partial texture display)
   */
  void renderVideoTexture(float x, float y, float width, float height, float uv_max_x);

  std::function<uint8_t(uint16_t)> memory_read_callback_;
  std::function<uint8_t(uint16_t)> aux_memory_read_callback_;
  std::function<void(uint8_t)> key_press_callback_;
  std::function<Apple2e::SoftSwitchState()> video_mode_callback_;

  // Character ROM (256 characters, 8 bytes each = 2KB per set, 2 sets = 4KB)
  // Primary set at offset 0, alternate set at offset 2048
  static constexpr size_t CHAR_SET_SIZE = 2048;      // Size of each character set
  static constexpr size_t CHAR_ROM_SIZE = 4096;      // Total: primary + alternate
  static constexpr size_t CHAR_ROM_ALT_OFFSET = 2048; // Offset to alternate set
  std::array<uint8_t, CHAR_ROM_SIZE> char_rom_;
  bool char_rom_loaded_ = false;

  // Display dimensions
  static constexpr int TEXT_WIDTH_40 = 40;
  static constexpr int TEXT_WIDTH_80 = 80;
  static constexpr int TEXT_HEIGHT = 24;
  static constexpr int CHAR_WIDTH = 7;
  static constexpr int CHAR_HEIGHT = 8;
  static constexpr int DISPLAY_WIDTH_40 = TEXT_WIDTH_40 * CHAR_WIDTH;   // 280 pixels
  static constexpr int DISPLAY_WIDTH_80 = TEXT_WIDTH_80 * CHAR_WIDTH;   // 560 pixels
  static constexpr int DISPLAY_HEIGHT = TEXT_HEIGHT * CHAR_HEIGHT;      // 192 pixels
  
  // Use 80-column width as the maximum display width
  static constexpr int DISPLAY_WIDTH = DISPLAY_WIDTH_80;
  
  // Legacy alias for compatibility
  static constexpr int TEXT_WIDTH = TEXT_WIDTH_40;

  // Frame buffer (RGBA)
  std::vector<uint32_t> frame_buffer_;

  // Metal texture handle
  void *texture_ = nullptr;  // id<MTLTexture>
  void *device_ = nullptr;   // id<MTLDevice>
  bool texture_initialized_ = false;

  // Metal sampler states for filtering modes
  void *sampler_linear_ = nullptr;   // id<MTLSamplerState>
  void *sampler_nearest_ = nullptr;  // id<MTLSamplerState>
  
  // Metal render pipeline for custom texture rendering
  void *render_pipeline_ = nullptr;  // id<MTLRenderPipelineState>
  void *vertex_buffer_ = nullptr;    // id<MTLBuffer>
  
  // Filtering mode: true = linear (smooth), false = nearest neighbor (sharp pixels)
  bool use_linear_filtering_ = false;
  
  // Color fringing: true = adjacent pixels blend to white (authentic), false = pure artifact colors
  bool color_fringing_enabled_ = true;

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

  // Refresh rate limiting
  int frame_skip_counter_ = 0;
  static constexpr int FRAME_SKIP = 2;  // Update every N frames (reduce GPU load)
  
  // Cache previous text page to detect changes
  // Main memory cache (40 chars per row)
  std::array<uint8_t, TEXT_WIDTH_40 * TEXT_HEIGHT> prev_text_page_main_;
  // Aux memory cache for 80-column mode (40 chars per row from aux bank)
  std::array<uint8_t, TEXT_WIDTH_40 * TEXT_HEIGHT> prev_text_page_aux_;
  bool prev_flash_state_ = false;
  bool needs_redraw_ = true;

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
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0
  };

  // Hi-res graphics has 192 lines with interleaved memory layout
  // Lines are grouped in sets of 8, with 3 groups of 64 lines each
  // Formula: base + (line % 8) * 0x400 + (line / 64) * 0x28 + ((line / 8) % 8) * 0x80

  // Colors (ABGR format for Metal)
  static constexpr uint32_t COLOR_BLACK = 0xFF000000;
  static constexpr uint32_t COLOR_WHITE = 0xFFFFFFFF;
  
  // Monochrome green phosphor
  static constexpr uint32_t COLOR_GREEN = 0xFF00FF00;
  
  // Hi-res NTSC artifact colors (Group 1: high bit = 0)
  static constexpr uint32_t COLOR_PURPLE = 0xFFFF00FF;   // Violet/Purple
  static constexpr uint32_t COLOR_GREEN_HIRES = 0xFF00FF00;  // Green
  
  // Hi-res NTSC artifact colors (Group 2: high bit = 1)
  static constexpr uint32_t COLOR_BLUE = 0xFFFF8000;     // Blue (in ABGR)
  static constexpr uint32_t COLOR_ORANGE = 0xFF0080FF;   // Orange (in ABGR)
  
  // Lo-res colors (16 colors)
  static constexpr uint32_t LORES_COLORS[16] = {
      0xFF000000,  // 0: Black
      0xFF0000E0,  // 1: Magenta (Dark Red)
      0xFF000080,  // 2: Dark Blue
      0xFF8000FF,  // 3: Purple (Violet)
      0xFF008000,  // 4: Dark Green
      0xFF808080,  // 5: Grey 1
      0xFFFF0000,  // 6: Medium Blue
      0xFFFF80C0,  // 7: Light Blue
      0xFF004080,  // 8: Brown
      0xFF0080FF,  // 9: Orange
      0xFF808080,  // 10: Grey 2
      0xFF80C0FF,  // 11: Pink
      0xFF00FF00,  // 12: Light Green (Green)
      0xFF00FFFF,  // 13: Yellow
      0xFFFFFF00,  // 14: Aqua
      0xFFFFFFFF   // 15: White
  };

  // Key repeat support
  int held_key_ = -1;              // Currently held key (ImGuiKey), -1 if none
  uint8_t held_apple_key_ = 0;     // Apple IIe keycode for held key
  float repeat_timer_ = 0.0f;      // Timer for repeat
  bool repeat_started_ = false;    // Has initial delay passed?
  
  // Apple IIe key repeat timing (approximately 10 repeats/second after 0.5s delay)
  static constexpr float REPEAT_DELAY = 0.5f;    // Initial delay before repeat starts
  static constexpr float REPEAT_RATE = 0.1f;     // Time between repeats (10/second)
};
