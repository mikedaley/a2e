#pragma once

#include "device.hpp"
#include "apple2e/memory_map.hpp"
#include "apple2e/soft_switches.hpp"
#include "ram.hpp"
#include "rom.hpp"
#include "keyboard.hpp"
#include "speaker.hpp"
#include "emulator/disk2.hpp"
#include "emulator/memory_access_tracker.hpp"
#include <memory>
#include <cstdint>

/**
 * MMU - Memory Management Unit
 *
 * The MMU handles bank switching, soft switches, and routes memory accesses
 * to the appropriate devices (RAM, ROM, I/O). It implements the Apple IIe
 * memory map and soft switch behavior.
 */
class MMU : public Device
{
public:
  /**
   * Constructs MMU with references to RAM and ROM
   * @param ram Reference to RAM device
   * @param rom Reference to ROM device
   * @param keyboard Optional pointer to keyboard device (can be nullptr)
   * @param speaker Optional pointer to speaker device (can be nullptr)
   * @param disk_ii Optional pointer to Disk II controller (can be nullptr)
   */
  MMU(RAM &ram, ROM &rom, Keyboard *keyboard = nullptr, Speaker *speaker = nullptr, DiskII *disk_ii = nullptr);

  /**
   * Destructor
   */
  ~MMU() override = default;

  // Delete copy constructor and assignment (non-copyable)
  MMU(const MMU &) = delete;
  MMU &operator=(const MMU &) = delete;

  // Allow move constructor, but delete move assignment (references cannot be moved)
  MMU(MMU &&) = default;
  MMU &operator=(MMU &&) = delete;

  /**
   * Read a byte through the MMU
   * Routes to RAM or ROM based on address and current configuration
   * NOTE: This may trigger side effects for soft switch reads
   * @param address 16-bit address
   * @return byte value
   */
  uint8_t read(uint16_t address) override;

  /**
   * Peek a byte through the MMU without triggering side effects
   * Use this for debuggers, memory viewers, etc.
   * @param address 16-bit address
   * @return byte value
   */
  uint8_t peek(uint16_t address) const;

  /**
   * Write a byte through the MMU
   * Routes to RAM or handles soft switches
   * @param address 16-bit address
   * @param value byte value
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this MMU occupies
   * @return AddressRange covering entire 64KB address space
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "MMU"
   */
  std::string getName() const override;

  /**
   * Get current soft switch state
   * @return reference to soft switch state
   */
  Apple2e::SoftSwitchState &getSoftSwitchState() { return soft_switches_; }

  /**
   * Get const current soft switch state
   * @return const reference to soft switch state
   */
  const Apple2e::SoftSwitchState &getSoftSwitchState() const { return soft_switches_; }

  /**
   * Get a snapshot of soft switch state including diagnostic values
   * This reads CSW/KSW from zero page for debugging purposes
   * @return copy of soft switch state with diagnostic values populated
   */
  Apple2e::SoftSwitchState getSoftSwitchSnapshot() const;

private:
  /**
   * Handle soft switch read
   * @param address Soft switch address
   * @return byte value (typically returns current state)
   */
  uint8_t readSoftSwitch(uint16_t address);

  /**
   * Handle soft switch write
   * @param address Soft switch address
   * @param value Written value (typically ignored, just toggles switch)
   */
  void writeSoftSwitch(uint16_t address, uint8_t value);

  /**
   * Handle bank switching soft switch
   * @param address Bank switch address
   */
  void handleBankSwitch(uint16_t address);

  /**
   * Handle language card soft switches ($C080-$C08F)
   * @param address Language card switch address
   */
  void handleLanguageCard(uint16_t address);

  RAM &ram_;
  ROM &rom_;
  Keyboard *keyboard_; // Optional, can be nullptr if keyboard is on bus separately
  Speaker *speaker_;   // Optional, can be nullptr
  DiskII *disk_ii_;    // Optional, can be nullptr
  Apple2e::SoftSwitchState soft_switches_;
  uint64_t cycle_count_ = 0; // Track cycles for speaker timing

public:
  /**
   * Update cycle count (call this to keep speaker timing accurate)
   * @param cycles Current CPU cycle count
   */
  void setCycleCount(uint64_t cycles) { cycle_count_ = cycles; }

  /**
   * Set the memory access tracker for visualization
   * @param tracker Pointer to tracker (can be nullptr to disable)
   */
  void setAccessTracker(memory_access_tracker *tracker) { access_tracker_ = tracker; }

private:
  memory_access_tracker *access_tracker_ = nullptr;
};
