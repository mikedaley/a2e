#pragma once

#include "device.hpp"
#include "apple2e/memory_map.hpp"
#include <array>
#include <cstdint>
#include <string>

/**
 * ROM - 16KB system ROM containing Monitor and Applesoft BASIC
 *
 * The ROM is read-only memory. Writes are silently ignored.
 * ROM data can be loaded from a file or set programmatically.
 */
class ROM : public Device
{
public:
  /**
   * Constructs ROM with all bytes initialized to 0xFF (unprogrammed)
   */
  ROM();

  /**
   * Destructor
   */
  ~ROM() override = default;

  // Delete copy constructor and assignment (non-copyable)
  ROM(const ROM &) = delete;
  ROM &operator=(const ROM &) = delete;

  // Allow move constructor and assignment
  ROM(ROM &&) = default;
  ROM &operator=(ROM &&) = default;

  /**
   * Read a byte from ROM
   * @param address 16-bit address
   * @return byte value from ROM
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to ROM (silently ignored - ROM is read-only)
   * @param address 16-bit address (ignored)
   * @param value byte value (ignored)
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this ROM occupies
   * @return AddressRange covering $D000-$FFFF
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "ROM"
   */
  std::string getName() const override;

  /**
   * Load ROM data from a file
   * @param filepath Path to ROM file
   * @return true on success, false on failure
   */
  bool loadFromFile(const std::string &filepath);

  /**
   * Load the standard Apple IIe ROMs from the include/roms directory
   * This loads both ROM chips (CD and EF) at the correct addresses
   * @return true on success, false on failure
   */
  bool loadAppleIIeROMs();

  /**
   * Load ROM data from a buffer
   * @param data Pointer to ROM data
   * @param size Size of ROM data (should be ROM_SIZE or less)
   * @return true on success, false on failure
   */
  bool loadFromData(const uint8_t *data, size_t size);

  /**
   * Get direct access to ROM data (for debugging)
   * @return reference to ROM array
   */
  std::array<uint8_t, Apple2e::ROM_SIZE> &getData() { return rom_data_; }

  /**
   * Get const access to ROM data
   * @return const reference to ROM array
   */
  const std::array<uint8_t, Apple2e::ROM_SIZE> &getData() const { return rom_data_; }

  /**
   * Read from the expansion ROM area ($C100-$CFFF)
   * This is the internal ROM that provides slot firmware when no card is present
   * @param address Address in the $C100-$CFFF range
   * @return byte value from expansion ROM
   */
  uint8_t readExpansionROM(uint16_t address);

  /**
   * Load Disk II P5A ROM at slot 6 location ($C600-$C6FF)
   * @param filepath Path to P5A ROM file (256 bytes)
   * @return true on success
   */
  bool loadDiskIIROM(const std::string &filepath);

private:
  std::array<uint8_t, Apple2e::ROM_SIZE> rom_data_;
  
  // Expansion ROM area ($C100-$CFFF) - 3840 bytes (0xF00)
  // This comes from the lower portion of the CD ROM chip
  static constexpr size_t EXPANSION_ROM_SIZE = 0x0F00;  // $C100-$CFFF
  std::array<uint8_t, EXPANSION_ROM_SIZE> expansion_rom_;

  /**
   * Load a ROM file at a specific offset in the ROM space
   * @param filepath Path to ROM file
   * @param offset Offset within the ROM space (0x0000-0x3FFF maps to $D000-$FFFF)
   * @param size Expected size of the ROM file
   * @return true on success, false on failure
   */
  bool loadROMAtOffset(const std::string &filepath, size_t offset, size_t size);
};

