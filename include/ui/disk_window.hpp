#pragma once

#include "ui/base_window.hpp"
#include <functional>
#include <string>

// Forward declarations
class emulator;

/**
 * disk_window - Displays real-time disk activity and allows disk management
 *
 * Shows disk controller state including track position, nibble position,
 * motor status, stepper phases, and allows loading/ejecting disk images.
 */
class disk_window : public base_window
{
public:
  /**
   * Constructs the disk window with callbacks to emulator
   */
  explicit disk_window(emulator& emu);

  /**
   * Update window state (fetch fresh disk state)
   */
  void update(float deltaTime) override;

  /**
   * Render the disk window UI
   */
  void render() override;

  /**
   * Get window name
   */
  const char* getName() const override { return "Disk Activity"; }

private:
  /**
   * Disk state structure for display
   */
  struct disk_state
  {
    // Drive 0 info
    int drive0_track = 0;
    int drive0_nibble_pos = 0;
    int drive0_sector = -1;
    bool drive0_has_disk = false;
    std::string drive0_filename;
    bool drive0_write_protected = false;

    // Drive 1 info
    int drive1_track = 0;
    int drive1_nibble_pos = 0;
    int drive1_sector = -1;
    bool drive1_has_disk = false;
    std::string drive1_filename;
    bool drive1_write_protected = false;

    // Controller state
    bool motor_on = false;
    int selected_drive = 0;
    uint8_t phase_mask = 0;
    bool q6 = false;
    bool q7 = false;
    uint8_t data_latch = 0;
  };

  /**
   * Context for async SDL file dialog callback
   */
  struct disk_load_context
  {
    std::function<bool(int, const std::string&)> load_callback;
    int drive;
  };

  /**
   * Render per-drive information section
   */
  void renderDriveInfo(int drive);

  /**
   * Render controller status section
   */
  void renderControllerState();

  /**
   * Handle disk loading for a drive
   */
  void handleDiskLoad(int drive);

  /**
   * Handle disk ejection for a drive
   */
  void handleDiskEject(int drive);

  /**
   * Extract filename from path for display
   */
  std::string getFilename(const std::string& path);

  // State and callbacks
  std::function<disk_state()> state_callback_;
  std::function<bool(int, const std::string&)> load_disk_callback_;
  std::function<void(int)> eject_disk_callback_;
  disk_state state_;
};
