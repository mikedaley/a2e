#pragma once

#include "ui/window_renderer.hpp"
#include "ui/window_manager.hpp"
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
   * Load window visibility from preferences
   */
  void loadWindowState();

  /**
   * Save window visibility to preferences
   */
  void saveWindowState();

  /**
   * Render modal dialogs (save/load state confirmation)
   */
  void renderDialogs();

  /**
   * Get the path for the save state file
   */
  std::string getSaveStatePath() const;

  /**
   * Request application close (triggers save dialog)
   */
  void requestClose();

  // UI components (declared first so they are destroyed last)
  // window_renderer_ must outlive emulator_ because emulator_ uses SDL audio
  // and video_display holds Metal textures created from window_renderer_'s device
  std::unique_ptr<window_renderer> window_renderer_;

  // Window manager (handles all UI windows)
  std::unique_ptr<window_manager> window_manager_;

  // Emulator (handles all Apple IIe emulation)
  // Destroyed before window_renderer_ to ensure SDL is still active for audio cleanup
  std::unique_ptr<emulator> emulator_;

  // Preferences for persistent state
  std::unique_ptr<preferences> preferences_;

  bool should_close_ = false;
  bool had_focus_ = true;   // Track focus state for speaker reset

  // Dialog state
  bool show_save_state_dialog_ = false;
};
