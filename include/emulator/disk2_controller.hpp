#pragma once

#include "device.hpp"
#include "disk_image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

/**
 * Disk2Controller - Disk II Controller Card for Slot 6
 *
 * The Disk II controller handles floppy disk I/O for the Apple IIe.
 * It occupies:
 * - I/O space: $C0E0-$C0EF (16 soft switches)
 * - Slot ROM: $C600-$C6FF (256 bytes, P5 ROM 341-0027)
 *
 * Soft switch addresses (accent to slot 6 base $C0E0):
 * $C0E0 - Phase 0 off     $C0E1 - Phase 0 on
 * $C0E2 - Phase 1 off     $C0E3 - Phase 1 on
 * $C0E4 - Phase 2 off     $C0E5 - Phase 2 on
 * $C0E6 - Phase 3 off     $C0E7 - Phase 3 on
 * $C0E8 - Motor off       $C0E9 - Motor on
 * $C0EA - Drive 1 select  $C0EB - Drive 2 select
 * $C0EC - Q6L (read)      $C0ED - Q6H (WP sense/write load)
 * $C0EE - Q7L (read mode) $C0EF - Q7H (write mode)
 */
class Disk2Controller : public Device
{
public:
  // Callback type for querying CPU cycle count
  using CycleCountCallback = std::function<uint64_t()>;

  /**
   * Constructs the Disk II controller
   */
  Disk2Controller();

  /**
   * Destructor
   */
  ~Disk2Controller() override = default;

  // Delete copy constructor and assignment (non-copyable)
  Disk2Controller(const Disk2Controller &) = delete;
  Disk2Controller &operator=(const Disk2Controller &) = delete;

  // Allow move
  Disk2Controller(Disk2Controller &&) = default;
  Disk2Controller &operator=(Disk2Controller &&) = default;

  /**
   * Initialize the controller and load the boot ROM
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * Reset the controller to power-on state
   * Resets all state except ROM contents
   */
  void reset();

  /**
   * Read a byte from the controller
   * Handles both I/O space ($C0E0-$C0EF) and slot ROM ($C600-$C6FF)
   * @param address 16-bit address
   * @return byte value
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to the controller
   * Handles I/O space soft switches ($C0E0-$C0EF)
   * @param address 16-bit address
   * @param value byte value
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this device occupies
   * Returns I/O range; slot ROM is handled separately
   * @return AddressRange covering $C0E0-$C0EF
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "Disk II Controller"
   */
  std::string getName() const override;

  /**
   * Check if address is in slot ROM range ($C600-$C6FF)
   * @param address Address to check
   * @return true if in slot ROM range
   */
  bool isSlotROMAddress(uint16_t address) const;

  /**
   * Read from slot ROM ($C600-$C6FF)
   * @param address Address in $C600-$C6FF range
   * @return ROM byte value
   */
  uint8_t readSlotROM(uint16_t address) const;

  /**
   * Set the callback for querying CPU cycle count
   * This allows the controller to query cycles on demand for timing-sensitive operations
   * @param callback Function that returns current CPU cycle count
   */
  void setCycleCallback(CycleCountCallback callback);

  /**
   * Get the current CPU cycle count
   * @return Current cycle count, or 0 if no callback is set
   */
  uint64_t getCycles() const;

  // ===== Disk operations =====

  /**
   * Insert a disk image into a drive
   * @param drive Drive number (0 or 1)
   * @param filename Path to disk image
   * @return true on success
   */
  bool insertDisk(int drive, const std::string &filename);

  /**
   * Eject disk from a drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  /**
   * Check if a drive has a disk inserted
   * @param drive Drive number (0 or 1)
   * @return true if disk is inserted
   */
  bool hasDisk(int drive) const;

  /**
   * Get the disk image for a drive (for UI display)
   * @param drive Drive number (0 or 1)
   * @return Pointer to disk image, or nullptr if no disk
   */
  const DiskImage *getDiskImage(int drive) const;

  /**
   * Check if motor is currently on
   * @return true if motor is running
   */
  bool isMotorOn() const;

  /**
   * Get currently selected drive (0 or 1)
   * @return Selected drive number
   */
  int getSelectedDrive() const;

  /**
   * Get the current phase magnet states
   * @return Bit field where bit 0-3 represent phases 0-3 (1 = on, 0 = off)
   */
  uint8_t getPhaseStates() const;

  /**
   * Get the current track position from the selected drive's disk image
   * @return Track number (0-34), or -1 if no disk
   */
  int getCurrentTrack() const;

  /**
   * Get the current quarter-track position from the selected drive's disk image
   * @return Quarter-track number (0-159), or -1 if no disk
   */
  int getQuarterTrack() const;

private:
  // Slot 6 I/O base address
  static constexpr uint16_t IO_BASE = 0xC0E0;
  static constexpr uint16_t IO_END = 0xC0EF;

  // Slot 6 ROM address range
  static constexpr uint16_t ROM_BASE = 0xC600;
  static constexpr uint16_t ROM_END = 0xC6FF;
  static constexpr size_t ROM_SIZE = 256;

  // Soft switch offsets from IO_BASE
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
  static constexpr uint8_t DRIVE1_SELECT = 0x0A;
  static constexpr uint8_t DRIVE2_SELECT = 0x0B;
  static constexpr uint8_t Q6L = 0x0C;  // Read data / shift
  static constexpr uint8_t Q6H = 0x0D;  // Write protect sense / write load
  static constexpr uint8_t Q7L = 0x0E;  // Read mode
  static constexpr uint8_t Q7H = 0x0F;  // Write mode

  // Controller state
  mutable bool motor_on_ = false;  // mutable for lazy timeout evaluation
  mutable uint64_t motor_off_cycle_ = 0;  // Cycle when motor-off was requested (0 = not pending)
  int selected_drive_ = 0;  // 0 or 1
  bool q6_ = false;         // Q6 latch state
  bool q7_ = false;         // Q7 latch state (false=read, true=write)
  uint8_t phase_states_ = 0; // Bit field for phase magnet states (for status display)

  // Motor timeout: ~1 second at 1.023 MHz
  static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;

  // Cycle count callback for timing
  CycleCountCallback cycle_callback_;

  // Slot ROM (256 bytes - P5 ROM 341-0027)
  std::array<uint8_t, ROM_SIZE> slot_rom_;
  bool rom_loaded_ = false;

  // Disk images for each drive
  std::unique_ptr<DiskImage> disk_images_[2];

  // Per-drive timing state
  uint64_t last_read_cycle_[2] = {0, 0};  // Cycle count of last read

  // Data latch (shift register)
  uint8_t data_latch_ = 0;
  bool latch_valid_ = false;  // True until first read of current nibble

  /**
   * Load the Disk II controller ROM (341-0027)
   * @return true on success
   */
  bool loadControllerROM();

  /**
   * Read a byte from the current disk
   * Updates disk timing and returns the data latch value
   * @return The data latch value
   */
  uint8_t readDiskData();

  /**
   * Handle soft switch access
   * @param offset Offset from IO_BASE (0x00-0x0F)
   * @param is_write true if write access, false if read
   * @return byte value for reads
   */
  uint8_t handleSoftSwitch(uint8_t offset, bool is_write);
};
