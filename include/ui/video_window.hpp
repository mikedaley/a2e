#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <functional>

/**
 * Video Window
 *
 * Displays the Apple IIe video output in an ImGui window.
 * This is a thin UI wrapper that:
 * - Displays a texture provided by video_display
 * - Handles keyboard input and passes it to the emulator
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
   * Set the texture to display
   * @param texture Metal texture pointer (id<MTLTexture>)
   */
  void setTexture(void *texture) { texture_ = texture; }

  /**
   * Set the current display width (for UV calculation)
   * @param width Current display width in pixels
   */
  void setCurrentDisplayWidth(int width) { current_display_width_ = width; }

  /**
   * Set the maximum display width (for UV calculation)
   * @param width Maximum display width in pixels
   */
  void setMaxDisplayWidth(int width) { max_display_width_ = width; }

  /**
   * Set the 40-column display width (for aspect ratio)
   * @param width 40-column display width in pixels
   */
  void setDisplayWidth40(int width) { display_width_40_ = width; }

  /**
   * Set the display height
   * @param height Display height in pixels
   */
  void setDisplayHeight(int height) { display_height_ = height; }

  /**
   * Set the key press callback
   * @param callback Function that handles a key press (Apple IIe key code)
   */
  void setKeyPressCallback(std::function<void(uint8_t)> callback);

private:
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

  // Texture from video_display
  void *texture_ = nullptr;

  // Display dimensions (set by application from video_display)
  int current_display_width_ = 280;
  int max_display_width_ = 560;
  int display_width_40_ = 280;
  int display_height_ = 192;

  // Key press callback
  std::function<void(uint8_t)> key_press_callback_;

  // Key repeat support
  int held_key_ = -1;          // Currently held key (ImGuiKey), -1 if none
  uint8_t held_apple_key_ = 0; // Apple IIe keycode for held key
  float repeat_timer_ = 0.0f;  // Timer for repeat
  bool repeat_started_ = false; // Has initial delay passed?

  // Apple IIe key repeat timing (approximately 10 repeats/second after 0.5s delay)
  static constexpr float REPEAT_DELAY = 0.5f; // Initial delay before repeat starts
  static constexpr float REPEAT_RATE = 0.1f;  // Time between repeats (10/second)
};
