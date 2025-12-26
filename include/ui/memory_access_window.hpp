#pragma once

#include "ui/base_window.hpp"
#include "emulator/memory_access_tracker.hpp"
#include <cstdint>
#include <functional>
#include <vector>

class emulator;

/**
 * memory_access_window - Visualizes memory access patterns
 *
 * Displays a 256x256 texture where each pixel represents one byte of
 * the 64KB memory space. Pixels are colored based on access type:
 * - Grey: No recent access
 * - Green: Recent read
 * - Red: Recent write
 *
 * Colors fade back to grey over time.
 */
class memory_access_window : public base_window
{
public:
  /**
   * Constructs the memory access window
   * @param emu Reference to the emulator for callbacks
   */
  explicit memory_access_window(emulator &emu);

  /**
   * Destructor - cleans up Metal texture
   */
  ~memory_access_window() override;

  /**
   * Initialize the Metal texture for display
   * @param device Metal device pointer
   * @return true on success
   */
  bool initializeTexture(void *device);

  /**
   * Update the window state (called each frame before render)
   * @param deltaTime Time elapsed since last frame in seconds
   */
  void update(float deltaTime) override;

  /**
   * Render the window contents
   */
  void render() override;

  /**
   * Get window name
   */
  const char *getName() const override { return "Memory Access"; }

private:
  /**
   * Update frame buffer from tracker state
   */
  void updateFrameBuffer();

  /**
   * Upload frame buffer to GPU texture
   */
  void uploadTexture();

  /**
   * Render tooltip for memory address under mouse
   * @param address Memory address to show info for
   */
  void renderTooltip(uint16_t address);

  /**
   * Interpolate between two colors
   * @param colorA Starting color (grey)
   * @param colorB Target color (green/red)
   * @param t Interpolation factor (0.0 to 1.0)
   * @return Interpolated color
   */
  uint32_t interpolateColor(uint32_t colorA, uint32_t colorB, float t) const;

  /**
   * Get memory region name for an address
   * @param address Memory address
   * @return Human-readable region name
   */
  const char *getRegionName(uint16_t address) const;

  // Texture dimensions (256x256 = 65536 pixels = 64KB)
  static constexpr int TEXTURE_SIZE = 256;

  // Frame buffer for texture data
  std::vector<uint32_t> frame_buffer_;

  // Metal resources
  void *texture_ = nullptr; // id<MTLTexture>
  void *device_ = nullptr;  // id<MTLDevice>
  bool texture_initialized_ = false;

  // Display state
  float zoom_level_ = 2.0f; // Start at 2x for better visibility
  static constexpr float MIN_ZOOM = 1.0f;
  static constexpr float MAX_ZOOM = 8.0f;

  // Callbacks
  std::function<memory_access_tracker *()> get_tracker_;
  std::function<uint8_t(uint16_t)> peek_memory_;

  // Colors (ABGR format for Metal RGBA8)
  static constexpr uint32_t COLOR_GREY = 0xFF808080;  // RGB(128,128,128)
  static constexpr uint32_t COLOR_GREEN = 0xFF00FF00; // RGB(0,255,0)
  static constexpr uint32_t COLOR_RED = 0xFF0000FF;   // RGB(255,0,0) - Note: ABGR
};
