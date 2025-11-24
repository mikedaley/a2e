#include "ram.hpp"

RAM::RAM()
{
  main_bank_.fill(0);
  aux_bank_.fill(0);
}

uint8_t RAM::read(uint16_t address)
{
  // Only handle addresses in RAM range
  if (address < Apple2e::MEM_RAM_START || address > Apple2e::MEM_RAM_END)
  {
    return 0xFF;
  }

  // Adjust address to be relative to RAM start
  uint16_t ram_address = address - Apple2e::MEM_RAM_START;

  if (read_aux_bank_)
  {
    return aux_bank_[ram_address];
  }
  else
  {
    return main_bank_[ram_address];
  }
}

void RAM::write(uint16_t address, uint8_t value)
{
  // Only handle addresses in RAM range
  if (address < Apple2e::MEM_RAM_START || address > Apple2e::MEM_RAM_END)
  {
    return;
  }

  // Adjust address to be relative to RAM start
  uint16_t ram_address = address - Apple2e::MEM_RAM_START;

  if (write_aux_bank_)
  {
    aux_bank_[ram_address] = value;
  }
  else
  {
    main_bank_[ram_address] = value;
  }
}

AddressRange RAM::getAddressRange() const
{
  return {Apple2e::MEM_RAM_START, Apple2e::MEM_RAM_END};
}

std::string RAM::getName() const
{
  return "RAM";
}

void RAM::setReadBank(bool aux_bank)
{
  read_aux_bank_ = aux_bank;
}

void RAM::setWriteBank(bool aux_bank)
{
  write_aux_bank_ = aux_bank;
}
