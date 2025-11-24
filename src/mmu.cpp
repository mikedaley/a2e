#include "mmu.hpp"

MMU::MMU(RAM &ram, ROM &rom, Keyboard *keyboard)
    : ram_(ram), rom_(rom), keyboard_(keyboard)
{
  // Initialize soft switches to default state
  soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
  soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
  soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
  soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
  soft_switches_.read_bank = Apple2e::MemoryBank::MAIN;
  soft_switches_.write_bank = Apple2e::MemoryBank::MAIN;
  soft_switches_.keyboard_strobe = false;
}

uint8_t MMU::read(uint16_t address)
{
  // Route based on address range
  if (address >= Apple2e::MEM_RAM_START && address <= Apple2e::MEM_RAM_END)
  {
    // Update RAM bank selection before reading
    ram_.setReadBank(soft_switches_.read_bank == Apple2e::MemoryBank::AUX);
    return ram_.read(address);
  }
  else if (address >= Apple2e::MEM_ROM_START && address <= Apple2e::MEM_ROM_END)
  {
    return rom_.read(address);
  }
  else if (address >= Apple2e::MEM_IO_START && address <= Apple2e::MEM_IO_END)
  {
    // Route keyboard I/O addresses to keyboard if available
    if (keyboard_ && (address == Apple2e::KB_DATA || address == Apple2e::KB_STROBE))
    {
      return keyboard_->read(address);
    }
    return readSoftSwitch(address);
  }

  // Unmapped address
  return 0xFF;
}

void MMU::write(uint16_t address, uint8_t value)
{
  // Route based on address range
  if (address >= Apple2e::MEM_RAM_START && address <= Apple2e::MEM_RAM_END)
  {
    // Update RAM bank selection before writing
    ram_.setWriteBank(soft_switches_.write_bank == Apple2e::MemoryBank::AUX);
    ram_.write(address, value);
  }
  else if (address >= Apple2e::MEM_ROM_START && address <= Apple2e::MEM_ROM_END)
  {
    // ROM is read-only, but some systems allow writes to ROM space
    // (they're ignored by the ROM device itself)
    rom_.write(address, value);
  }
  else if (address >= Apple2e::MEM_IO_START && address <= Apple2e::MEM_IO_END)
  {
    // Route keyboard I/O addresses to keyboard if available
    if (keyboard_ && (address == Apple2e::KB_DATA || address == Apple2e::KB_STROBE))
    {
      keyboard_->write(address, value);
      return;
    }
    writeSoftSwitch(address, value);
  }
  // Writes to unmapped addresses are silently ignored
}

AddressRange MMU::getAddressRange() const
{
  // MMU handles the entire address space
  return {0x0000, 0xFFFF};
}

std::string MMU::getName() const
{
  return "MMU";
}

uint8_t MMU::readSoftSwitch(uint16_t address)
{
  // Reading soft switches typically returns the current state
  // Some switches return specific values
  switch (address)
  {
  case Apple2e::SW_TEXT_MODE:
    return soft_switches_.video_mode == Apple2e::VideoMode::TEXT ? 0x00 : 0xFF;
  case Apple2e::SW_GRAPHICS_MODE:
    return soft_switches_.video_mode == Apple2e::VideoMode::GRAPHICS ? 0x00 : 0xFF;
  case Apple2e::SW_FULL_SCREEN:
    return soft_switches_.screen_mode == Apple2e::ScreenMode::FULL ? 0x00 : 0xFF;
  case Apple2e::SW_MIXED_MODE:
    return soft_switches_.screen_mode == Apple2e::ScreenMode::MIXED ? 0x00 : 0xFF;
  case Apple2e::SW_PAGE1:
    return soft_switches_.page_select == Apple2e::PageSelect::PAGE1 ? 0x00 : 0xFF;
  case Apple2e::SW_PAGE2:
    return soft_switches_.page_select == Apple2e::PageSelect::PAGE2 ? 0x00 : 0xFF;
  case Apple2e::SW_LORES:
    return soft_switches_.graphics_mode == Apple2e::GraphicsMode::LORES ? 0x00 : 0xFF;
  case Apple2e::SW_HIRES:
    return soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES ? 0x00 : 0xFF;
  default:
    // Bank switching addresses return current state
    if (address >= Apple2e::BANK_READ_MAIN && address <= Apple2e::BANK_READ_AUX_WRITE_AUX)
    {
      return 0x00; // Bank switches return 0 when read
    }
    return 0xFF;
  }
}

void MMU::writeSoftSwitch(uint16_t address, uint8_t value)
{
  (void)value; // Value is typically ignored, just the address matters

  switch (address)
  {
  case Apple2e::SW_TEXT_MODE:
    soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
    break;
  case Apple2e::SW_GRAPHICS_MODE:
    soft_switches_.video_mode = Apple2e::VideoMode::GRAPHICS;
    break;
  case Apple2e::SW_FULL_SCREEN:
    soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
    break;
  case Apple2e::SW_MIXED_MODE:
    soft_switches_.screen_mode = Apple2e::ScreenMode::MIXED;
    break;
  case Apple2e::SW_PAGE1:
    soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
    break;
  case Apple2e::SW_PAGE2:
    soft_switches_.page_select = Apple2e::PageSelect::PAGE2;
    break;
  case Apple2e::SW_LORES:
    soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
    break;
  case Apple2e::SW_HIRES:
    soft_switches_.graphics_mode = Apple2e::GraphicsMode::HIRES;
    break;
  default:
    // Handle bank switching
    if (address >= Apple2e::BANK_READ_MAIN && address <= Apple2e::BANK_READ_AUX_WRITE_AUX)
    {
      handleBankSwitch(address);
    }
    break;
  }
}

void MMU::handleBankSwitch(uint16_t address)
{
  // Bank switching addresses control read/write bank selection
  // The address determines the combination of read/write banks
  switch (address)
  {
  case Apple2e::BANK_READ_MAIN:
    soft_switches_.read_bank = Apple2e::MemoryBank::MAIN;
    break;
  case Apple2e::BANK_READ_AUX:
    soft_switches_.read_bank = Apple2e::MemoryBank::AUX;
    break;
  case Apple2e::BANK_WRITE_MAIN:
    soft_switches_.write_bank = Apple2e::MemoryBank::MAIN;
    break;
  case Apple2e::BANK_WRITE_AUX:
    soft_switches_.write_bank = Apple2e::MemoryBank::AUX;
    break;
  case Apple2e::BANK_READ_MAIN_WRITE_AUX:
    soft_switches_.read_bank = Apple2e::MemoryBank::MAIN;
    soft_switches_.write_bank = Apple2e::MemoryBank::AUX;
    break;
  case Apple2e::BANK_READ_AUX_WRITE_MAIN:
    soft_switches_.read_bank = Apple2e::MemoryBank::AUX;
    soft_switches_.write_bank = Apple2e::MemoryBank::MAIN;
    break;
  case Apple2e::BANK_READ_MAIN_WRITE_MAIN:
    soft_switches_.read_bank = Apple2e::MemoryBank::MAIN;
    soft_switches_.write_bank = Apple2e::MemoryBank::MAIN;
    break;
  case Apple2e::BANK_READ_AUX_WRITE_AUX:
    soft_switches_.read_bank = Apple2e::MemoryBank::AUX;
    soft_switches_.write_bank = Apple2e::MemoryBank::AUX;
    break;
  }
}

