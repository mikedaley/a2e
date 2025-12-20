#include "keyboard.hpp"

Keyboard::Keyboard() = default;

uint8_t Keyboard::read(uint16_t address)
{
  if (address == Apple2e::KB_DATA)
  {
    // Reading $C000 returns the last key code
    // Bit 7 (0x80) is set if a key is available (strobe set)
    // Bit 7 is clear if no key is available
    if (!key_queue_.empty())
    {
      uint8_t key_code = key_queue_.front();
      strobe_cleared_ = false;
      return key_code | 0x80; // Bit 7 set = key available
    }
    // No key pressed: return 0x00 with bit 7 clear
    return 0x00;
  }
  else if (address == Apple2e::KB_STROBE)
  {
    // Reading $C010 clears the strobe and returns the strobe status
    // Bit 7 set = strobe was set (key was available)
    // Bit 7 clear = strobe was already cleared
    uint8_t result = strobe_cleared_ ? 0x00 : 0x80;
    if (!strobe_cleared_ && !key_queue_.empty())
    {
      key_queue_.pop();
      strobe_cleared_ = true;
    }
    return result;
  }

  // For any other address in the keyboard range, return 0x00
  return 0x00;
}

void Keyboard::write(uint16_t address, uint8_t value)
{
  // Keyboard is read-only, writes are ignored
  (void)address;
  (void)value;
}

AddressRange Keyboard::getAddressRange() const
{
  return {Apple2e::KB_DATA, Apple2e::KB_STROBE};
}

std::string Keyboard::getName() const
{
  return "Keyboard";
}

void Keyboard::keyDown(uint8_t key_code)
{
  // Only add to queue if not already pressed (debounce)
  if (pressed_keys_.find(key_code) == pressed_keys_.end())
  {
    pressed_keys_.insert(key_code);
    key_queue_.push(key_code);
    strobe_cleared_ = false;
  }
}

void Keyboard::keyUp(uint8_t key_code)
{
  pressed_keys_.erase(key_code);
}

