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
  std::function<bool()> q6_callback_;
  std::function<bool()> q7_callback_;
  std::function<bool()> write_mode_callback_;
  std::function<uint8_t()> data_latch_callback_;

  // Callbacks for disk operations
  std::function<bool(int)> has_disk_callback_;
  std::function<const DiskImage*(int)> get_disk_image_callback_;
  std::function<bool(int, const std::string&)> insert_disk_callback_;
  std::function<void(int)> eject_disk_callback_;
  std::function<bool(int, const std::string&)> create_disk_callback_;

  // File browser dialogs
  std::unique_ptr<FileBrowserDialog> file_browser_;      // For loading disks
  std::unique_ptr<FileBrowserDialog> save_file_browser_; // For creating new disks
  int pending_drive_ = 0;  // Which drive to load/create into
};
