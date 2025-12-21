#pragma once

#include <MOS6502/CPU6502.hpp>
#include <window/window_renderer.hpp>
#include <windows/cpu_window.hpp>
#include <windows/memory_viewer_window.hpp>
#include <windows/video_window.hpp>
#include <windows/disk_window.hpp>
#include <bus.hpp>
#include <ram.hpp>
#include <rom.hpp>
#include <mmu.hpp>
#include <keyboard.hpp>
#include <speaker.hpp>
#include <disk2.hpp>
#include <preferences.hpp>
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

  /**
   * Load a disk image into drive 1 or 2
   * @param drive Drive number (0 or 1)
   * @param filepath Path to .dsk/.do file
   * @return true on success
   */
  bool loadDisk(int drive, const std::string &filepath);

  /**
   * Eject disk from drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  // Forward declaration to avoid template complexity in header
  class cpu_wrapper;

  // Core emulator components
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<RAM> ram_;
  std::unique_ptr<ROM> rom_;
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<Keyboard> keyboard_;
  std::unique_ptr<Speaker> speaker_;
  std::unique_ptr<DiskII> disk2_;
  std::unique_ptr<cpu_wrapper> cpu_;

  // UI components
  std::unique_ptr<window_renderer> window_renderer_;
  std::unique_ptr<cpu_window> cpu_window_;
  std::unique_ptr<memory_viewer_window> memory_viewer_window_;
  std::unique_ptr<video_window> video_window_;
  std::unique_ptr<disk_window> disk_window_;

  // Preferences for persistent state
  std::unique_ptr<preferences> preferences_;

  bool should_close_ = false;
  bool had_focus_ = true;   // Track focus state for speaker reset
  bool first_update_ = true; // Track first update to sync speaker timing
  
  // Pending disk load from file dialog (set by callback, processed in update)
  std::string pending_disk_path_;
  int pending_disk_drive_ = -1;
  
  /**
   * Show file dialog to select a disk image
   * @param drive Drive number (0 or 1)
   */
  void showDiskFileDialog(int drive);
  
  /**
   * Static callback for SDL file dialog
   */
  static void diskFileDialogCallback(void *userdata, const char * const *filelist, int filter);
};
