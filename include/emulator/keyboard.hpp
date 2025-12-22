#pragma once

#include "device.hpp"
#include "apple2e/soft_switches.hpp"
#include <cstdint>

/**
 * Keyboard - Apple IIe keyboard input handling
 *
 * The Apple IIe keyboard uses a latch-based system:
 * - When a key is pressed, the keycode is latched and bit 7 (strobe) is set
 * - Reading $C000 returns the latched keycode with bit 7 indicating key waiting
 * - Reading or writing $C010 clears bit 7 (strobe) but keycode remains latched
 * - The next keypress overwrites the latched keycode and sets bit 7 again
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
   * @return byte value (key code with strobe if $C000, any-key-down if $C010)
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to keyboard
   * @param address 16-bit address
   * @param value byte value (ignored - writing $C010 clears strobe)
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this keyboard occupies
   * @return AddressRange covering $C000-$C01F
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "Keyboard"
   */
  std::string getName() const override;

  /**
   * Handle a key press event from the host system
   * Latches the keycode and sets the strobe bit
   * @param key_code Apple IIe key code (0-127)
   */
  void keyDown(uint8_t key_code);

  /**
   * Handle a key release event from the host system
   * Clears the any-key-down flag for this key
   * @param key_code Apple IIe key code (0-127)
   */
  void keyUp(uint8_t key_code);

  /**
   * Check if keyboard strobe is set (key available)
   * @return true if a key is waiting
   */
  bool isStrobeSet() const { return key_waiting_; }

private:
  // Latched keycode (7-bit, 0-127)
  uint8_t latched_keycode_ = 0;
  
  // Strobe flag - set when key pressed, cleared by reading/writing $C010
  bool key_waiting_ = false;
  
  // Any-key-down flag - true while any key is physically held
  bool any_key_down_ = false;
};
