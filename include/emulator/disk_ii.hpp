#pragma once

#include "device.hpp"
#include "disk_image.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * DiskII - Emulates the Apple II Disk II controller card
 *
 * The Disk II controller provides soft switches at $C0x0-$C0xF where x = $8 + slot.
 * For the standard slot 6, this is $C0E0-$C0EF.
 *
 * The controller handles:
 * - Stepper motor control (4 phases) for head positioning
 * - Motor on/off with 1-second shutoff delay
 * - Drive selection (drive 1 or 2)
 * - Read/write mode selection
 * - Data latch for reading/writing nibbles
 */
class DiskII : public Device
{
public:
  // Soft switch offsets (add to base address $C080 + slot * 16)
  static constexpr uint8_t PHASE0_OFF = 0x00;
  static constexpr uint8_t PHASE0_ON  = 0x01;
  static constexpr uint8_t PHASE1_OFF = 0x02;
  static constexpr uint8_t PHASE1_ON  = 0x03;
  static constexpr uint8_t PHASE2_OFF = 0x04;
  static constexpr uint8_t PHASE2_ON  = 0x05;
  static constexpr uint8_t PHASE3_OFF = 0x06;
  static constexpr uint8_t PHASE3_ON  = 0x07;
  static constexpr uint8_t MOTOR_OFF  = 0x08;
  static constexpr uint8_t MOTOR_ON   = 0x09;
  static constexpr uint8_t DRIVE1     = 0x0A;
  static constexpr uint8_t DRIVE2     = 0x0B;
  static constexpr uint8_t Q6L        = 0x0C;  // Shift (read mode)
  static constexpr uint8_t Q6H        = 0x0D;  // Load (write mode) / write protect
  static constexpr uint8_t Q7L        = 0x0E;  // Read mode
  static constexpr uint8_t Q7H        = 0x0F;  // Write mode

  // Timing constants
  static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;  // ~1 second at 1.023 MHz
  static constexpr int CYCLES_PER_BIT = 4;  // 4 CPU cycles per bit cell

  /**
   * Constructor
   * @param slot Slot number (typically 6)
   */
  explicit DiskII(int slot = 6);

  /**
   * Destructor
   */
  ~DiskII() override = default;

  // Device interface
  uint8_t read(uint16_t address) override;
  void write(uint16_t address, uint8_t value) override;
  AddressRange getAddressRange() const override;
  std::string getName() const override;

  /**
   * Update disk state (call periodically with current CPU cycle count)
   * Handles motor shutoff timing and disk rotation
   * @param cpuCycles Current total CPU cycle count
   */
  void update(uint64_t cpuCycles);

  /**
   * Insert a disk into a drive
   * @param drive Drive number (0 or 1)
   * @param filepath Path to disk image file
   * @return true on success
   */
  bool insertDisk(int drive, const std::string& filepath);

  /**
   * Eject the disk from a drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  /**
   * Check if a disk is inserted
   * @param drive Drive number (0 or 1)
   */
  bool isDiskInserted(int drive) const;

  /**
   * Check if a disk is write protected
   * @param drive Drive number (0 or 1)
   */
  bool isWriteProtected(int drive) const;

  /**
   * Set write protection on a disk
   * @param drive Drive number (0 or 1)
   * @param protect true to write protect
   */
  void setWriteProtected(int drive, bool protect);

  /**
   * Get the current track number
   */
  int getCurrentTrack() const { return current_half_track_ / 2; }

  /**
   * Get the current half-track position (0-69)
   */
  int getCurrentHalfTrack() const { return current_half_track_; }

  /**
   * Check if motor is on
   */
  bool isMotorOn() const { return motor_on_; }

  /**
   * Get selected drive (0 or 1)
   */
  int getSelectedDrive() const { return selected_drive_; }

  /**
   * Get the disk image for a drive (for UI display)
   */
  const DiskImage* getDiskImage(int drive) const;

  /**
   * Save state to stream
   */
  void saveState(std::ostream& out) const;

  /**
   * Load state from stream
   */
  void loadState(std::istream& in);

private:
  // Drive state structure
  struct Drive
  {
    std::unique_ptr<DiskImage> disk;
    std::vector<uint8_t> nibble_track;  // Current track's nibblized data
    int current_track = 0;              // Track loaded in nibble_track
    size_t byte_position = 0;           // Current position within track
    bool write_protected = true;
    bool track_dirty = false;           // Track has been modified
  };

  int slot_;
  uint16_t base_address_;

  // Hardware state
  int selected_drive_ = 0;        // 0 or 1
  bool motor_on_ = false;
  uint8_t phase_states_ = 0;      // Bits 0-3 = phases 0-3 on/off
  int current_half_track_ = 0;    // 0-69 (35 tracks * 2 half-tracks)

  // Controller latches
  bool q6_ = false;               // Q6 latch: false = shift, true = load
  bool q7_ = false;               // Q7 latch: false = read, true = write
  uint8_t data_latch_ = 0;        // Data register

  // Timing
  uint64_t last_cycle_ = 0;       // Last CPU cycle count
  uint64_t last_read_cycle_ = 0;  // Cycle count of last byte read
  uint64_t motor_off_cycle_ = 0;  // When motor will shut off
  bool motor_off_pending_ = false;
  bool data_valid_ = false;       // True when new byte is ready to read

  // Drives
  std::array<Drive, 2> drives_;

  /**
   * Handle a soft switch access
   * @param offset Offset from base address (0-15)
   * @param isWrite true if write access
   * @param value Value being written (ignored for reads)
   * @return Value for reads
   */
  uint8_t handleSoftSwitch(uint8_t offset, bool isWrite, uint8_t value);

  /**
   * Update stepper motor phase state
   * @param phase Phase number (0-3)
   * @param on true to turn on, false to turn off
   */
  void setPhase(int phase, bool on);

  /**
   * Move the head based on current phase states
   */
  void moveHead();

  /**
   * Load the current track into nibble buffer
   */
  void loadCurrentTrack();

  /**
   * Flush any dirty track data back to disk image
   */
  void flushTrack();

  /**
   * Advance disk position based on elapsed cycles
   * @param cycles Number of CPU cycles elapsed
   */
  void advancePosition(uint64_t cycles);

  /**
   * Read from the data latch
   */
  uint8_t readDataLatch();

  /**
   * Write to the data latch
   */
  void writeDataLatch(uint8_t value);

  /**
   * Get current drive
   */
  Drive& currentDrive() { return drives_[selected_drive_]; }
  const Drive& currentDrive() const { return drives_[selected_drive_]; }
};
