#pragma once

#include "ui/base_window.hpp"
#include "ui/cpu_window.hpp"
#include "ui/memory_viewer_window.hpp"
#include "ui/video_window.hpp"
#include "ui/soft_switches_window.hpp"
#include "ui/debugger_window.hpp"
#include "ui/memory_access_window.hpp"
#include "ui/disk_window.hpp"
#include <memory>
#include <vector>

// Forward declarations
class emulator;
class preferences;

/**
 * window_manager - Manages all application windows
 *
 * This class handles creation, configuration, updating, and rendering
 * of all UI windows. It provides a clean separation between window
 * management and the main application logic.
 */
class window_manager
{
public:
  /**
   * Constructs the window manager
   */
  window_manager();

  /**
   * Destructor
   */
  ~window_manager();

  // Non-copyable
  window_manager(const window_manager&) = delete;
  window_manager& operator=(const window_manager&) = delete;

  // Movable
  window_manager(window_manager&&) = default;
  window_manager& operator=(window_manager&&) = default;

  /**
   * Initialize all windows with emulator callbacks
   * @param emu The emulator instance for setting up callbacks
   * @param metal_device Metal device for video texture initialization
   */
  void initialize(emulator& emu, void* metal_device);

  /**
   * Update all windows
   * @param deltaTime Time elapsed since last frame in seconds
   */
  void update(float deltaTime);

  /**
   * Render all windows
   */
  void render();

  /**
   * Load window visibility states from preferences
   * @param prefs Preferences instance
   */
  void loadState(preferences& prefs);

  /**
   * Save window visibility states to preferences
   * @param prefs Preferences instance
   */
  void saveState(preferences& prefs);

  // Accessors for specific windows (for menu toggles)
  cpu_window* getCPUWindow() { return cpu_window_; }
  memory_viewer_window* getMemoryViewerWindow() { return memory_viewer_window_; }
  video_window* getVideoWindow() { return video_window_; }
  soft_switches_window* getSoftSwitchesWindow() { return soft_switches_window_; }
  debugger_window* getDebuggerWindow() { return debugger_window_; }
  memory_access_window* getMemoryAccessWindow() { return memory_access_window_; }
  disk_window* getDiskWindow() { return disk_window_; }

private:
  // All windows managed by the window manager (ownership held here)
  std::vector<std::unique_ptr<base_window>> windows_;

  // Typed pointers for specific window access (non-owning)
  cpu_window* cpu_window_ = nullptr;
  memory_viewer_window* memory_viewer_window_ = nullptr;
  video_window* video_window_ = nullptr;
  soft_switches_window* soft_switches_window_ = nullptr;
  debugger_window* debugger_window_ = nullptr;
  memory_access_window* memory_access_window_ = nullptr;
  disk_window* disk_window_ = nullptr;
};
