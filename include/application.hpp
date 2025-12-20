#pragma once

#include <MOS6502/CPU6502.hpp>
#include <window/window_renderer.hpp>
#include <windows/cpu_window.hpp>
#include <windows/memory_viewer_window.hpp>
#include <windows/text_screen_window.hpp>
#include <bus.hpp>
#include <ram.hpp>
#include <rom.hpp>
#include <mmu.hpp>
#include <keyboard.hpp>
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

  // Forward declaration to avoid template complexity in header
  class cpu_wrapper;

  // Core emulator components
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<RAM> ram_;
  std::unique_ptr<ROM> rom_;
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<Keyboard> keyboard_;
  std::unique_ptr<cpu_wrapper> cpu_;

  // UI components
  std::unique_ptr<window_renderer> window_renderer_;
  std::unique_ptr<cpu_window> cpu_window_;
  std::unique_ptr<memory_viewer_window> memory_viewer_window_;
  std::unique_ptr<text_screen_window> text_screen_window_;

  bool should_close_ = false;
};
