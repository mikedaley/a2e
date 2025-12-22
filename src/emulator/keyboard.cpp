#include "emulator/keyboard.hpp"

Keyboard::Keyboard()
    : latched_keycode_(0), key_waiting_(false), any_key_down_(false)
{
}

uint8_t Keyboard::read(uint16_t address)
{
  if (address == Apple2e::KB_DATA || (address >= 0xC000 && address <= 0xC00F))
  {
    // Reading $C000-$C00F returns the latched keycode
    // Bit 7 is set if a key is waiting (strobe set)
    if (key_waiting_)
    {
      return latched_keycode_ | 0x80;
    }
    else
    {
      return latched_keycode_;  // Return last keycode but with bit 7 clear
    }
  }
  else if (address == Apple2e::KB_STROBE || (address >= 0xC010 && address <= 0xC01F))
  {
    // Reading $C010-$C01F clears the strobe AND returns any-key-down status
    // Bit 7 = any key currently held down (IIe feature)
    // Bits 6-0 = last keycode
    uint8_t result = latched_keycode_;
    if (any_key_down_)
    {
      result |= 0x80;
    }
    
    // Clear the strobe (key waiting flag)
    key_waiting_ = false;
    
    return result;
  }

  return 0x00;
}

void Keyboard::write(uint16_t address, uint8_t value)
{
  (void)value;  // Value is ignored
  
  // Writing to $C010-$C01F clears the keyboard strobe
  if (address >= 0xC010 && address <= 0xC01F)
  {
    key_waiting_ = false;
  }
}

AddressRange Keyboard::getAddressRange() const
{
  return {Apple2e::KB_DATA, 0xC01F};
}

std::string Keyboard::getName() const
{
  return "Keyboard";
}

void Keyboard::keyDown(uint8_t key_code)
{
  // Latch the keycode (only 7 bits)
  latched_keycode_ = key_code & 0x7F;
  
  // Set the strobe - indicates a new key is waiting
  key_waiting_ = true;
  
  // Set any-key-down flag
  any_key_down_ = true;
}

void Keyboard::keyUp(uint8_t key_code)
{
  (void)key_code;
  
  // Clear any-key-down flag
  // Note: In a full implementation, we'd track which keys are held
  // and only clear this when all keys are released
  any_key_down_ = false;
}
