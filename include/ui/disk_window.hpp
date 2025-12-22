#pragma once

#include "base_window.hpp"
#include <string>
#include <functional>

// Forward declaration
class emulator;

/**
 * disk_window - Disk II drive management window
 *
 * Provides UI for:
 * - Inserting and ejecting disk images
 * - Viewing drive status (motor, track, disk name)
 * - Write protection toggle
 * - Drive activity indicator
 */
class disk_window : public base_window
{
public:
  /**
   * Constructs a disk window
   * @param emu Reference to the emulator for disk operations
   */
  explicit disk_window(emulator& emu);

  /**
   * Update the window state
   * @param deltaTime Time elapsed since last frame in seconds
   */
  void update(float deltaTime) override;

  /**
   * Render the disk window
   */
  void render() override;

  /**
   * Get window name
   */
  const char* getName() const override { return "Disk Drives"; }

private:
  emulator& emu_;

  // File dialog state
  bool show_file_dialog_ = false;
  int file_dialog_drive_ = 0;
  std::string current_path_;
  std::string selected_file_;
  std::vector<std::string> directory_entries_;

  // Activity indicator timing
  float drive_activity_timer_[2] = {0.0f, 0.0f};
  bool last_motor_state_[2] = {false, false};

  // Helper methods
  void renderDrivePanel(int drive);
  void renderFileDialog();
  void refreshDirectoryListing();
  void openFileDialog(int drive);
  bool isValidDiskImage(const std::string& filename) const;
  std::string getFilenameFromPath(const std::string& path) const;
};
