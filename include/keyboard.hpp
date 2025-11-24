#pragma once

#include "device.hpp"
#include "apple2e/soft_switches.hpp"
#include <cstdint>
#include <queue>
#include <unordered_set>

/**
 * Keyboard - Apple IIe keyboard input handling
 *
 * The Apple IIe keyboard uses a matrix scanning system. Keys are read
 * from address $C000, and the strobe is cleared by reading $C010.
 */
class Keyboard : public Device
{
public:
  /**
   * Constructs keyboard
   */
  Keyboard();

  /**
   * Destructor
   */
  ~Keyboard() override = default;

  // Delete copy constructor and assignment (non-copyable)
  Keyboard(const Keyboard &) = delete;
  Keyboard &operator=(const Keyboard &) = delete;

  // Allow move constructor and assignment
  Keyboard(Keyboard &&) = default;
  Keyboard &operator=(Keyboard &&) = default;

  /**
   * Read a byte from keyboard
   * @param address 16-bit address
   * @return byte value (key code if address is $C000, strobe status if $C010)
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to keyboard (no-op, keyboard is read-only)
   * @param address 16-bit address (ignored)
   * @param value byte value (ignored)
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this keyboard occupies
   * @return AddressRange covering $C000-$C010
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "Keyboard"
   */
  std::string getName() const override;

  /**
   * Handle a key press event from the host system
   * @param key_code Apple IIe key code (0-127)
   */
  void keyDown(uint8_t key_code);

  /**
   * Handle a key release event from the host system
   * @param key_code Apple IIe key code (0-127)
   */
  void keyUp(uint8_t key_code);

  /**
   * Check if keyboard strobe is set (key available)
   * @return true if a key is waiting
   */
  bool isStrobeSet() const { return !key_queue_.empty(); }

private:
  std::queue<uint8_t> key_queue_;
  std::unordered_set<uint8_t> pressed_keys_;
  bool strobe_cleared_ = true;
};

