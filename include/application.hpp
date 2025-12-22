#pragma once

#include "ui/window_renderer.hpp"
#include "ui/cpu_window.hpp"
#include "ui/memory_viewer_window.hpp"
#include "ui/video_window.hpp"
#include "ui/soft_switches_window.hpp"
#include "emulator/emulator.hpp"
#include "preferences.hpp"
#include <memory>

/**
 * application - Main application class that manages UI and window state
 * 
 * This class handles the application lifecycle, window management, menus,
 * and preferences. The actual emulation is delegated to the emulator class.
 */
class application
{
public:
  /**
   * Constructs the application
   */
  application();

  /**
   * Destructor
   */
  ~application();

  // Delete copy constructor and assignment (non-copyable)
  application(const application &) = delete;
  application &operator=(const application &) = delete;

  // Allow move constructor and assignment
  application(application &&) = default;
  application &operator=(application &&) = default;

  /**
   * Initialize the application
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * Run the application main loop
   * @return Exit code (0 on success)
   */
  int run();

private:
  /**
   * Render callback for IMGUI
   */
  void renderUI();

  /**
   * Update callback (called each frame)
   */
  void update(float deltaTime);

  /**
   * Setup IMGUI windows and UI
   */
  void setupUI();

  /**
   * Render menu bar
   */
  void renderMenuBar();

  /**
   * Update CPU window state
   */
  void updateCPUWindow();

  /**
   * Load window visibility from preferences
   */
  void loadWindowState();

  /**
   * Save window visibility to preferences
   */
  void saveWindowState();

  // UI components (declared first so they are destroyed last)
  // window_renderer_ must outlive emulator_ because emulator_ uses SDL audio
  // and video_display holds Metal textures created from window_renderer_'s device
  std::unique_ptr<window_renderer> window_renderer_;
  std::unique_ptr<cpu_window> cpu_window_;
  std::unique_ptr<memory_viewer_window> memory_viewer_window_;
  std::unique_ptr<video_window> video_window_;
  std::unique_ptr<soft_switches_window> soft_switches_window_;

  // Emulator (handles all Apple IIe emulation)
  // Destroyed before window_renderer_ to ensure SDL is still active for audio cleanup
  std::unique_ptr<emulator> emulator_;

  // Preferences for persistent state
  std::unique_ptr<preferences> preferences_;

  bool should_close_ = false;
  bool had_focus_ = true;   // Track focus state for speaker reset
};
