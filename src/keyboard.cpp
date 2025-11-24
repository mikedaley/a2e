#include "keyboard.hpp"

Keyboard::Keyboard() = default;

uint8_t Keyboard::read(uint16_t address)
{
  if (address == Apple2e::KB_DATA)
  {
    // Reading $C000 returns the key code with bit 7 set if strobe is set
    if (!key_queue_.empty())
    {
      uint8_t key_code = key_queue_.front();
      strobe_cleared_ = false;
      return key_code | 0x80; // Bit 7 indicates key available
    }
    return 0x00; // No key available
  }
  else if (address == Apple2e::KB_STROBE)
  {
    // Reading $C010 clears the strobe
    uint8_t result = strobe_cleared_ ? 0x00 : 0x80;
    if (!strobe_cleared_ && !key_queue_.empty())
    {
      key_queue_.pop();
      strobe_cleared_ = true;
    }
    return result;
  }

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

