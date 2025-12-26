#include "emulator/rom.hpp"
#include "utils/resource_path.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

ROM::ROM()
{
  // Initialize ROM to 0xFF (unprogrammed state)
  rom_data_.fill(0xFF);
  expansion_rom_.fill(0xFF);
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

bool ROM::loadROMAtOffset(const std::string &filepath, size_t offset, size_t size)
{
  // Use the resource path utility to locate the ROM file
  std::string fullPath = getResourcePath(filepath);
  std::ifstream file(fullPath, std::ios::binary);
  if (!file.is_open())
  {
    std::cerr << "Failed to load ROM: " << filepath << " (tried path: " << fullPath << ")" << std::endl;
    return false;
  }

  // Verify offset and size are within bounds
  if (offset + size > rom_data_.size())
  {
    std::cerr << "ROM file too large: " << filepath << " (offset: " << offset << ", size: " << size << ")" << std::endl;
    file.close();
    return false;
  }

  // Read the file data
  file.read(reinterpret_cast<char *>(rom_data_.data() + offset), size);
  size_t bytes_read = file.gcount();
  file.close();

  if (bytes_read != size)
  {
    std::cerr << "Warning: ROM file " << filepath << " size mismatch (expected: " << size << ", read: " << bytes_read << ")" << std::endl;
    return false;
  }

  std::cout << "Successfully loaded ROM: " << filepath << " at offset $" << std::hex << (Apple2e::MEM_ROM_START + offset) << std::dec << std::endl;
  return true;
}

uint8_t ROM::readExpansionROM(uint16_t address)
{
  // Handle addresses in the $C100-$CFFF range
  if (address >= 0xC100 && address <= 0xCFFF)
  {
    uint16_t offset = address - 0xC100;
    return expansion_rom_[offset];
  }
  return 0xFF;
}

bool ROM::loadAppleIIeROMs()
{
  std::cout << "Loading Apple IIe ROMs..." << std::endl;

  // Try to load the 342-0349-B Enhanced Apple IIe ROM first
  // This is a 16KB file containing $C000-$FFFF (complete ROM image)
  const char *rom_349 = "resources/roms/system/342-0349-B-C0-FF.bin";
  std::string fullPath = getResourcePath(rom_349);
  std::ifstream file(fullPath, std::ios::binary);
  
  if (file.is_open())
  {
    // Check file size - should be 16KB for $C000-$FFFF
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    constexpr size_t FULL_ROM_SIZE = 0x4000;  // 16KB
    
    if (file_size == FULL_ROM_SIZE)
    {
      // 342-0349-B is a 16KB file containing $C000-$FFFF:
      // - Offset $0000-$00FF: I/O space placeholder (not used)
      // - Offset $0100-$0FFF: Expansion ROM ($C100-$CFFF)
      // - Offset $1000-$3FFF: Main ROM ($D000-$FFFF)
      
      // Load expansion ROM area ($C100-$CFFF)
      file.seekg(0x0100);
      file.read(reinterpret_cast<char *>(expansion_rom_.data()), EXPANSION_ROM_SIZE);
      size_t exp_bytes_read = file.gcount();
      
      if (exp_bytes_read != EXPANSION_ROM_SIZE)
      {
        std::cerr << "Expansion ROM size mismatch" << std::endl;
        file.close();
        return false;
      }
      
      // Load main ROM area ($D000-$FFFF)
      file.seekg(0x1000);
      file.read(reinterpret_cast<char *>(rom_data_.data()), Apple2e::ROM_SIZE);
      size_t rom_bytes_read = file.gcount();
      file.close();
      
      if (rom_bytes_read == Apple2e::ROM_SIZE)
      {
        std::cout << "Successfully loaded ROM from " << rom_349 << std::endl;
        std::cout << "  Expansion ROM ($C100-$CFFF): " << EXPANSION_ROM_SIZE << " bytes" << std::endl;
        std::cout << "  Main ROM ($D000-$FFFF): " << Apple2e::ROM_SIZE << " bytes" << std::endl;
        
        // Verify reset vector
        uint8_t reset_lo = read(0xFFFC);
        uint8_t reset_hi = read(0xFFFD);
        uint16_t reset_vector = reset_lo | (reset_hi << 8);
        std::cout << "Reset vector: $" << std::hex << std::uppercase << reset_vector << std::dec << std::endl;
        
        std::cout << "Apple IIe Enhanced ROMs loaded successfully!" << std::endl;
        return true;
      }
    }
    file.close();
  }
  
  // Fall back to loading separate CD and EF ROM files
  std::cout << "342-0349-B ROM not found, trying separate ROM files..." << std::endl;
  
  // Apple IIe ROM layout:
  // The physical ROM chips are:
  // - 342-0135-A (CD): 8KB chip containing code for $C000-$DFFF
  //   - $C000-$C0FF: I/O space (not ROM, handled by MMU)
  //   - $C100-$CFFF: Expansion ROM / internal slot firmware (3840 bytes)
  //   - $D000-$DFFF: Monitor ROM (4KB)
  // - 342-0134-A (EF): 8KB chip containing code for $E000-$FFFF
  //
  // We load:
  // - Bytes 0x0100-0x0FFF of CD chip into expansion_rom_ ($C100-$CFFF)
  // - Bytes 0x1000-0x1FFF of CD chip into rom_data_ at offset 0 ($D000-$DFFF)
  // - Full 8KB of EF chip into rom_data_ at offset 0x1000 ($E000-$FFFF)

  const char *rom_cd = "resources/roms/system/342-0135-A-CD.bin";
  const char *rom_ef = "resources/roms/system/342-0134-A-EF.bin";

  bool success = true;

  // Load the CD ROM
  fullPath = getResourcePath(rom_cd);
  std::ifstream file_cd(fullPath, std::ios::binary);
  if (!file_cd.is_open())
  {
    std::cerr << "Failed to open CD ROM: " << rom_cd << " (tried path: " << fullPath << ")" << std::endl;
    success = false;
  }
  else
  {
    // First, load the expansion ROM area ($C100-$CFFF)
    // This is bytes 0x0100-0x0FFF in the CD ROM file (3840 bytes = 0xF00)
    file_cd.seekg(0x0100);
    file_cd.read(reinterpret_cast<char *>(expansion_rom_.data()), EXPANSION_ROM_SIZE);
    size_t exp_bytes_read = file_cd.gcount();
    
    if (exp_bytes_read == EXPANSION_ROM_SIZE)
    {
      std::cout << "Successfully loaded CD ROM expansion area ($C100-$CFFF)" << std::endl;
    }
    else
    {
      std::cerr << "CD ROM expansion area size mismatch (expected: " << EXPANSION_ROM_SIZE 
                << ", read: " << exp_bytes_read << ")" << std::endl;
      success = false;
    }
    
    // Then load the upper 4KB ($D000-$DFFF)
    file_cd.seekg(0x1000);
    file_cd.read(reinterpret_cast<char *>(rom_data_.data()), 0x1000);
    size_t bytes_read = file_cd.gcount();
    file_cd.close();

    if (bytes_read == 0x1000)
    {
      std::cout << "Successfully loaded CD ROM (upper 4KB) at $D000" << std::endl;
    }
    else
    {
      std::cerr << "CD ROM size mismatch (expected: 4096, read: " << bytes_read << ")" << std::endl;
      success = false;
    }
  }

  // Load the EF ROM - full 8KB that maps to $E000-$FFFF
  if (!loadROMAtOffset(rom_ef, 0x1000, 0x2000))  // 8KB at offset 4KB
  {
    std::cerr << "Failed to load EF ROM" << std::endl;
    success = false;
  }

  if (success)
  {
    std::cout << "Apple IIe ROMs loaded successfully!" << std::endl;

    // Verify reset vector
    uint8_t reset_lo = read(0xFFFC);
    uint8_t reset_hi = read(0xFFFD);
    uint16_t reset_vector = reset_lo | (reset_hi << 8);
    std::cout << "Reset vector: $" << std::hex << std::uppercase << reset_vector << std::dec << std::endl;
  }

  return success;
}
