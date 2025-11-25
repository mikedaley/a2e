#pragma once

#include "device.hpp"
#include "apple2e/memory_map.hpp"
#include "apple2e/soft_switches.hpp"
#include "ram.hpp"
#include "rom.hpp"
#include "keyboard.hpp"
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
   */
  MMU(RAM &ram, ROM &rom, Keyboard *keyboard = nullptr);

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
   * @param address 16-bit address
   * @return byte value
   */
  uint8_t read(uint16_t address) override;

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

  RAM &ram_;
  ROM &rom_;
  Keyboard *keyboard_; // Optional, can be nullptr if keyboard is on bus separately
  Apple2e::SoftSwitchState soft_switches_;
};
