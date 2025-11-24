#pragma once

#include "device.hpp"
#include "apple2e/memory_map.hpp"
#include "apple2e/soft_switches.hpp"
#include "ram.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>

/**
 * Video - Apple IIe video output with text and graphics modes
 *
 * Handles all video modes:
 * - Text: 40/80 column, 24 lines
 * - Graphics: Lo-res (40×48), Hi-res (280×192)
 * - Mixed modes
 *
 * Renders to an SDL3 texture that can be displayed in the main window.
 */
class Video : public Device
{
public:
  /**
   * Constructs video component
   * @param ram Reference to RAM for accessing video memory
   */
  explicit Video(RAM &ram);

  /**
   * Destructor
   */
  ~Video() override;

  // Delete copy constructor and assignment (non-copyable)
  Video(const Video &) = delete;
  Video &operator=(const Video &) = delete;

  // Allow move constructor and assignment
  Video(Video &&) = default;
  Video &operator=(Video &&) = default;

  /**
   * Read a byte from video memory or soft switches
   * @param address 16-bit address
   * @return byte value
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to video memory or soft switches
   * @param address 16-bit address
   * @param value byte value
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this video component occupies
   * @return AddressRange covering video memory and soft switches
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "Video"
   */
  std::string getName() const override;

  /**
   * Initialize video rendering
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * Render the current video frame
   * Updates the internal surface with current video memory contents
   */
  void render();

  /**
   * Get the video surface (for display in main window)
   * @return SDL3 surface, or nullptr if not initialized
   */
  SDL_Surface *getSurface() const { return surface_; }

  /**
   * Get raw pixel data (for ImGui display)
   * @return pointer to pixel data, or nullptr if not initialized
   */
  const uint32_t *getPixels() const;

  /**
   * Get pixel data size
   * @return number of pixels
   */
  size_t getPixelCount() const;

  /**
   * Get video dimensions
   * @return pair of (width, height) in pixels
   */
  std::pair<int, int> getDimensions() const;

  /**
   * Update soft switch state (called by MMU)
   * @param state Current soft switch state
   */
  void updateSoftSwitches(const Apple2e::SoftSwitchState &state);

private:
  /**
   * Render text mode (40 column)
   */
  void renderTextMode();

  /**
   * Render lo-res graphics mode (40×48)
   */
  void renderLoResMode();

  /**
   * Render hi-res graphics mode (280×192)
   */
  void renderHiResMode();

  /**
   * Get pixel color for lo-res graphics
   * @param byte Video byte
   * @param bit_pair Which bit pair (0-3)
   * @return RGB color
   */
  uint32_t getLoResColor(uint8_t byte, int bit_pair) const;

  /**
   * Get pixel color for hi-res graphics
   * @param byte1 First byte of pixel pair
   * @param byte2 Second byte of pixel pair
   * @param bit_offset Bit offset within the pair
   * @return RGB color
   */
  uint32_t getHiResColor(uint8_t byte1, uint8_t byte2, int bit_offset) const;

  /**
   * Convert Apple IIe color to RGB
   * @param color_index Color index (0-15)
   * @return RGB color (0xRRGGBB)
   */
  uint32_t colorIndexToRGB(int color_index) const;

  RAM &ram_;
  SDL_Surface *surface_ = nullptr;
  Apple2e::SoftSwitchState soft_switches_;

  // Video dimensions
  static constexpr int TEXT_WIDTH = 40;
  static constexpr int TEXT_HEIGHT = 24;
  static constexpr int LORES_WIDTH = 40;
  static constexpr int LORES_HEIGHT = 48;
  static constexpr int HIRES_WIDTH = 280;
  static constexpr int HIRES_HEIGHT = 192;

  // Pixel scaling (for display)
  static constexpr int PIXEL_SCALE = 2;
};

