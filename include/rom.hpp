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

private:
  std::array<uint8_t, Apple2e::ROM_SIZE> rom_data_;
};

