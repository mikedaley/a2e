#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <functional>

// Forward declarations
class video_display;
class emulator;

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
   * @param emu Reference to the emulator for video display and keyboard input
   */
  explicit video_window(emulator& emu);

  /**
   * Destructor
   */
  ~video_window() override;

  /**
   * Update the video display (generates new frame)
   * @param deltaTime Time elapsed since last frame in seconds
   */
  void update(float deltaTime) override;

  /**
   * Render the video window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Video Display"; }

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

  // Video display (owned by application)
  video_display *video_display_ = nullptr;

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
