#pragma once

#include <MOS6502/CPU6502.hpp>
#include "ui/window_renderer.hpp"
#include "ui/cpu_window.hpp"
#include "ui/memory_viewer_window.hpp"
#include "ui/video_window.hpp"
#include "ui/soft_switches_window.hpp"
#include "emulator/video_display.hpp"
#include "emulator/bus.hpp"
#include "emulator/ram.hpp"
#include "emulator/rom.hpp"
#include "emulator/mmu.hpp"
#include "emulator/keyboard.hpp"
#include "emulator/speaker.hpp"
#include "preferences.hpp"
#include <memory>
#include <functional>
#include <cstdint>

/**
 * application - Main application class that manages the emulator state and UI
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

  /**
   * Hard reset - simulate power cycle (cold boot)
   */
  void reset();

  /**
   * Warm reset - jump to BASIC prompt without memory clear
   * Simulates pressing Ctrl+Reset
   */
  void warmReset();

  // Forward declaration to avoid template complexity in header
  class cpu_wrapper;

  // Core emulator components
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<RAM> ram_;
  std::unique_ptr<ROM> rom_;
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<Keyboard> keyboard_;
  std::unique_ptr<Speaker> speaker_;
  std::unique_ptr<video_display> video_display_;
  std::unique_ptr<cpu_wrapper> cpu_;

  // UI components
  std::unique_ptr<window_renderer> window_renderer_;
  std::unique_ptr<cpu_window> cpu_window_;
  std::unique_ptr<memory_viewer_window> memory_viewer_window_;
  std::unique_ptr<video_window> video_window_;
  std::unique_ptr<soft_switches_window> soft_switches_window_;

  // Preferences for persistent state
  std::unique_ptr<preferences> preferences_;

  bool should_close_ = false;
  bool had_focus_ = true;   // Track focus state for speaker reset
  bool first_update_ = true; // Track first update to sync speaker timing
};
