#pragma once

#include "base_window.hpp"
#include "file_browser_dialog.hpp"
#include <functional>
#include <cstdint>
#include <memory>
#include <string>

// Forward declarations
class emulator;
class DiskImage;

/**
 * Disk Window
 *
 * Displays the current state of the Disk II controller.
 * Shows LED indicators for motor status and disk ready state.
 * Provides controls for loading and ejecting disk images.
 */
class disk_window : public base_window
{
public:
  /**
   * Constructor
   * @param emu Reference to the emulator for accessing disk controller state
   */
  explicit disk_window(emulator& emu);

  /**
   * Destructor
   */
  ~disk_window() override = default;

  /**
   * Render the window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Disk II Controller"; }

private:
  /**
   * Render an LED indicator
   * @param label Display label for the LED
   * @param state Current state (true = on/lit)
   * @param on_color Color when LED is on (default green)
   * @param off_color Color when LED is off (default dark grey)
   */
  void renderLED(const char *label, bool state,
                 uint32_t on_color = 0xFF00FF00,
                 uint32_t off_color = 0xFF333333);

  /**
   * Render a section header
   * @param label Section title
   */
  void renderSectionHeader(const char *label);

  /**
   * Render disk info for a specific drive
   * @param drive Drive number (0 or 1)
   */
  void renderDrivePanel(int drive);

  /**
   * Get just the filename from a full path
   */
  static std::string getFilename(const std::string &path);

  // Callbacks for controller state
  std::function<bool()> motor_on_callback_;
  std::function<bool()> disk_ready_callback_;
  std::function<int()> selected_drive_callback_;
  std::function<uint8_t()> phase_states_callback_;
  std::function<int()> current_track_callback_;
  std::function<int()> quarter_track_callback_;

  // Callbacks for disk operations
  std::function<bool(int)> has_disk_callback_;
  std::function<const DiskImage*(int)> get_disk_image_callback_;
  std::function<bool(int, const std::string&)> insert_disk_callback_;
  std::function<void(int)> eject_disk_callback_;

  // File browser dialog
  std::unique_ptr<FileBrowserDialog> file_browser_;
  int pending_drive_ = 0;  // Which drive to load into
};
