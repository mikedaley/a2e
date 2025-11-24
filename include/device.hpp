#pragma once

#include <cstdint>
#include <string>

/**
 * AddressRange - Represents a memory address range for a device
 */
struct AddressRange
{
  uint16_t start;
  uint16_t end;

  /**
   * Check if an address falls within this range
   */
  bool contains(uint16_t address) const
  {
    return address >= start && address <= end;
  }
};

/**
 * Device - Base interface for all hardware components
 *
 * All Apple IIe hardware components implement this interface to enable
 * bus-based communication. This allows for easy extension with new devices.
 */
class Device
{
public:
  virtual ~Device() = default;

  /**
   * Read a byte from the device at the given address
   * @param address 16-bit address
   * @return byte value read from device
   */
  virtual uint8_t read(uint16_t address) = 0;

  /**
   * Write a byte to the device at the given address
   * @param address 16-bit address
   * @param value byte value to write
   */
  virtual void write(uint16_t address, uint8_t value) = 0;

  /**
   * Get the address range this device occupies
   * @return AddressRange with start and end addresses
   */
  virtual AddressRange getAddressRange() const = 0;

  /**
   * Get the name of this device (for debugging/logging)
   * @return device name string
   */
  virtual std::string getName() const = 0;
};
