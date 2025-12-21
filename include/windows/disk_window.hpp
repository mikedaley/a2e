#pragma once

#include "base_window.hpp"
#include "disk2.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <deque>

/**
 * disk_window - Disk II controller debugging window
 *
 * Displays comprehensive information about the Disk II controller including:
 * - Controller state (motor, drive select, Q6/Q7 modes)
 * - Current track and sector position
 * - Stepper motor phase status
 * - Recent nibbles read from disk
 * - Disk image information
 */
class disk_window : public base_window
{
public:
  /**
   * Constructs a disk window
   */
  disk_window();

  /**
   * Render the disk window
   */
  void render() override;

  /**
   * Get window name
   */
  const char *getName() const override { return "Disk II Controller"; }

  /**
   * Set the DiskII controller to monitor
   * @param disk2 Pointer to the DiskII controller
   */
  void setDiskController(DiskII *disk2) { disk2_ = disk2; }

  /**
   * Log a nibble read (call from disk controller)
   */
  void logNibbleRead(uint8_t nibble, int track, int position);

  /**
   * Log a soft switch access
   */
  void logSoftSwitch(uint16_t address, bool is_write, uint8_t value);

private:
  DiskII *disk2_ = nullptr;

  // Nibble history for display
  static constexpr int MAX_NIBBLE_HISTORY = 64;
  struct NibbleEntry {
    uint8_t nibble;
    int track;
    int position;
  };
  std::deque<NibbleEntry> nibble_history_;

  // Soft switch access history
  static constexpr int MAX_SWITCH_HISTORY = 32;
  struct SwitchEntry {
    uint16_t address;
    bool is_write;
    uint8_t value;
    std::string name;
  };
  std::deque<SwitchEntry> switch_history_;

  // UI state
  bool show_nibble_history_ = true;
  bool show_switch_history_ = true;
  bool auto_scroll_ = true;

  // Helper methods
  void renderControllerState();
  void renderDriveState(int drive);
  void renderNibbleHistory();
  void renderSwitchHistory();
  
  static const char* getSoftSwitchName(uint16_t address);
};
