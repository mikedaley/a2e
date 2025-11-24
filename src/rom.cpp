#include "rom.hpp"
#include <fstream>
#include <algorithm>

ROM::ROM()
{
  // Initialize ROM to 0xFF (unprogrammed state)
  rom_data_.fill(0xFF);
}

uint8_t ROM::read(uint16_t address)
{
  // Only handle addresses in ROM range
  if (address < Apple2e::MEM_ROM_START || address > Apple2e::MEM_ROM_END)
  {
    return 0xFF;
  }

  // Adjust address to be relative to ROM start
  uint16_t rom_address = address - Apple2e::MEM_ROM_START;

  if (rom_address < rom_data_.size())
  {
    return rom_data_[rom_address];
  }

  return 0xFF;
}

void ROM::write(uint16_t address, uint8_t value)
{
  // ROM is read-only, silently ignore writes
  (void)address;
  (void)value;
}

AddressRange ROM::getAddressRange() const
{
  return {Apple2e::MEM_ROM_START, Apple2e::MEM_ROM_END};
}

std::string ROM::getName() const
{
  return "ROM";
}

bool ROM::loadFromFile(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open())
  {
    return false;
  }

  // Read file into buffer
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size > rom_data_.size())
  {
    file_size = rom_data_.size();
  }

  file.read(reinterpret_cast<char *>(rom_data_.data()), file_size);

  // Fill remaining bytes with 0xFF if file is smaller than ROM size
  if (file_size < rom_data_.size())
  {
    std::fill(rom_data_.begin() + file_size, rom_data_.end(), 0xFF);
  }

  return true;
}

bool ROM::loadFromData(const uint8_t *data, size_t size)
{
  if (!data)
  {
    return false;
  }

  size_t copy_size = std::min(size, rom_data_.size());
  std::copy(data, data + copy_size, rom_data_.begin());

  // Fill remaining bytes with 0xFF if data is smaller than ROM size
  if (copy_size < rom_data_.size())
  {
    std::fill(rom_data_.begin() + copy_size, rom_data_.end(), 0xFF);
  }

  return true;
}
