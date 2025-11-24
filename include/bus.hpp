#pragma once

#include "device.hpp"
#include <memory>
#include <vector>
#include <cstdint>

/**
 * Bus - Central communication hub for all hardware components
 *
 * The bus routes memory read/write operations to the appropriate device
 * based on address ranges. This enables a modular architecture where
 * new devices can be easily added by implementing the Device interface.
 */
class Bus
{
public:
  /**
   * Constructs an empty bus
   */
  Bus();

  /**
   * Destructor
   */
  ~Bus();

  // Delete copy constructor and assignment (non-copyable)
  Bus(const Bus &) = delete;
  Bus &operator=(const Bus &) = delete;

  // Allow move constructor and assignment
  Bus(Bus &&) = default;
  Bus &operator=(Bus &&) = default;

  /**
   * Register a device with the bus
   * @param device Unique pointer to device to register
   * @note The bus takes ownership of the device
   */
  void registerDevice(std::unique_ptr<Device> device);

  /**
   * Unregister a device by name
   * @param name Device name to remove
   * @return true if device was found and removed
   */
  bool unregisterDevice(const std::string &name);

  /**
   * Read a byte from the bus at the given address
   * Routes to the appropriate device based on address range
   * @param address 16-bit address
   * @return byte value read from device, or 0xFF if no device handles the address
   */
  uint8_t read(uint16_t address) const;

  /**
   * Write a byte to the bus at the given address
   * Routes to the appropriate device based on address range
   * @param address 16-bit address
   * @param value byte value to write
   */
  void write(uint16_t address, uint8_t value) const;

  /**
   * Get the number of registered devices
   * @return device count
   */
  size_t getDeviceCount() const;

  /**
   * Get a device by name (for debugging)
   * @param name Device name
   * @return pointer to device, or nullptr if not found
   */
  Device *getDevice(const std::string &name) const;

private:
  /**
   * Find the device that handles the given address
   * Uses priority-based resolution (last registered device wins)
   * @param address Address to look up
   * @return pointer to device, or nullptr if no device handles the address
   */
  Device *findDevice(uint16_t address) const;

  std::vector<std::unique_ptr<Device>> devices_;
};
