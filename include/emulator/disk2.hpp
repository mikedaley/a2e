#pragma once

#include "emulator/device.hpp"
#include "emulator/disk_image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/**
 * DiskII - Disk II Controller Emulation
 *
 * Emulates the Disk II controller card in slot 6.
 * Implements the Device interface for bus-based access.
 * Soft switches at $C0E0-$C0EF control:
 * - Stepper motor phases (head positioning)
 * - Drive motor on/off
 * - Drive 1/2 selection
 * - Read/write mode
 * - Data register shifting
 */
class DiskII : public Device
{
public:
  // Slot 6 soft switch addresses
  static constexpr uint16_t SLOT6_BASE = 0xC0E0;

  // Soft switch offsets from base
  static constexpr uint8_t PHASE0_OFF = 0x00;
  static constexpr uint8_t PHASE0_ON = 0x01;
  static constexpr uint8_t PHASE1_OFF = 0x02;
  static constexpr uint8_t PHASE1_ON = 0x03;
  static constexpr uint8_t PHASE2_OFF = 0x04;
  static constexpr uint8_t PHASE2_ON = 0x05;
  static constexpr uint8_t PHASE3_OFF = 0x06;
  static constexpr uint8_t PHASE3_ON = 0x07;
  static constexpr uint8_t MOTOR_OFF = 0x08;
  static constexpr uint8_t MOTOR_ON = 0x09;
  static constexpr uint8_t DRIVE1_SEL = 0x0A;
  static constexpr uint8_t DRIVE2_SEL = 0x0B;
  static constexpr uint8_t Q6L = 0x0C; // Shift data register
  static constexpr uint8_t Q6H = 0x0D; // Load data register / sense write protect
  static constexpr uint8_t Q7L = 0x0E; // Read mode
  static constexpr uint8_t Q7H = 0x0F; // Write mode

  // Timing constants (in CPU cycles at 1.023 MHz)
  // Actual: 204,600 cycles/rotation ÷ 6656 nibbles = 30.74 cycles/nibble
  static constexpr uint64_t CYCLES_PER_NIBBLE = 31;     // 31 is closer to 30.74 than 32
  static constexpr uint64_t MOTOR_SPINUP_CYCLES = 0; // ~0.5 second spinup

  DiskII();
  ~DiskII() override = default;

  // Delete copy (disk state shouldn't be copied)
  DiskII(const DiskII &) = delete;
  DiskII &operator=(const DiskII &) = delete;

  // Device interface implementation
  uint8_t read(uint16_t address) override;
  void write(uint16_t address, uint8_t value) override;
  AddressRange getAddressRange() const override;
  std::string getName() const override;

  /**
   * Set the current CPU cycle count for timing-accurate reads
   * @param cycle Current CPU cycle count
   */
  void setCycleCount(uint64_t cycle);

  /**
   * Reset the controller state (but keep disk inserted)
   */
  void reset();

  /**
   * Insert a disk image into drive 1 or 2
   * @param drive Drive number (0 or 1)
   * @param image Disk image to insert
   * @return true on success
   */
  bool insertDisk(int drive, std::unique_ptr<DiskImage> image);

  /**
   * Eject disk from drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  /**
   * Check if a drive has a disk inserted
   */
  bool hasDisk(int drive) const;

  /**
   * Get current track for a drive (whole track number, for compatibility)
   */
  int getCurrentTrack(int drive) const;

  /**
   * Get current quarter-track position for a drive (0-139)
   * Each whole track = 4 quarter-tracks, so track 17.25 = quarter-track 69
   */
  int getQuarterTrack(int drive) const;

  /**
   * Get current track position as a float (e.g., 17.25, 17.50, 17.75)
   * Useful for UI display showing actual head position
   */
  float getTrackPosition(int drive) const;

  /**
   * Check if motor is on (for UI display)
   */
  bool isMotorOn() const { return motor_on_; }

  /**
   * Get the currently selected drive (0 or 1)
   */
  int getSelectedDrive() const { return selected_drive_; }

  /**
   * Get Q6 state (false=shift, true=load)
   */
  bool getQ6() const { return q6_; }

  /**
   * Get Q7 state (false=read, true=write)
   */
  bool getQ7() const { return q7_; }

  /**
   * Get stepper phase bitmask (bits 0-3 = phases 0-3)
   */
  uint8_t getPhaseMask() const { return current_phase_; }

  /**
   * Get current nibble position within track
   */
  int getNibblePosition(int drive) const;

  /**
   * Get data latch value
   */
  uint8_t getDataLatch() const { return data_latch_; }

  /**
   * Get disk image for a drive (for UI display of filename and write protection)
   * @param drive Drive number (0 or 1)
   * @return Pointer to DiskImage or nullptr if no disk inserted
   */
  const DiskImage* getDiskImage(int drive) const
  {
    if (drive >= 0 && drive < 2 && drives_[drive].disk)
    {
      return drives_[drive].disk.get();
    }
    return nullptr;
  }

  /**
   * Set callback for soft switch access logging
   * @param callback Function called with (address, is_write, value)
   */
  void setSoftSwitchCallback(std::function<void(uint16_t, bool, uint8_t)> callback)
  {
    soft_switch_callback_ = std::move(callback);
  }

  /**
   * Set callback for nibble read logging
   * @param callback Function called with (nibble, track, position)
   */
  void setNibbleReadCallback(std::function<void(uint8_t, int, int)> callback)
  {
    nibble_read_callback_ = std::move(callback);
  }

  /**
   * Set callback to get current PC for tracing
   * @param callback Function that returns current program counter
   */
  void setPCCallback(std::function<uint16_t()> callback)
  {
    pc_callback_ = std::move(callback);
  }

private:
  /**
   * Internal access handler (shared by read/write)
   * @param address Address in $C0E0-$C0EF range
   * @return Data latch value for reads
   */
  uint8_t accessInternal(uint16_t address);

  /**
   * Update head position based on stepper motor phase changes
   */
  void updateHeadPosition();

  /**
   * Read next nibble from current track position
   */
  uint8_t readNibble();

  /**
   * Advance track position based on elapsed cycles
   */
  void advancePosition();

  // Drive state - each drive tracks its own position
  struct DriveState
  {
    std::unique_ptr<DiskImage> disk;
    int quarter_track = 0;           // Current quarter-track position (0-139, where 0=track 0, 4=track 1, etc.)
    int nibble_position = 0;         // Current byte position within track
    uint64_t last_read_cycle = 0;    // Last cycle when a nibble was successfully read
    uint64_t last_returned_nibble = 0; // Total nibble count when last nibble was returned with bit 7 set
    int last_returned_track = -1;    // Last track returned with bit 7 set
  };

  std::array<DriveState, 2> drives_;

  // Controller state
  int selected_drive_ = 0;
  bool motor_on_ = false;
  uint64_t accumulated_on_cycles_ = 0;  // Total cycles motor has been on
  uint64_t last_motor_update_cycle_ = 0; // Last cycle we updated accumulated time

  // Stepper motor phase bitmask (bits 0-3 represent phases 0-3)
  // This tracks which phases are currently energized
  uint8_t current_phase_ = 0;

  // Data latch and mode
  uint8_t data_latch_ = 0;
  bool q6_ = false; // false = shift, true = load
  bool q7_ = false; // false = read, true = write

  // Timing
  uint64_t cycle_count_ = 0;

  // Gap tracking for PC tracing
  uint64_t last_q6l_cycle_ = 0;
  uint64_t last_pc_log_cycle_ = 0;
  bool motor_was_on_ = false;
  int pc_sample_count_ = 0;

  // Debug callbacks
  std::function<void(uint16_t, bool, uint8_t)> soft_switch_callback_;
  std::function<void(uint8_t, int, int)> nibble_read_callback_;
  std::function<uint16_t()> pc_callback_;
};
