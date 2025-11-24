#pragma once

#include "device.hpp"
#include "apple2e/memory_map.hpp"
#include <array>
#include <cstdint>

/**
 * RAM - 64KB main memory with aux bank support
 *
 * The Apple IIe has two 64KB memory banks: main and aux.
 * Bank selection is controlled by the MMU via soft switches.
 */
class RAM : public Device
{
public:
  /**
   * Constructs RAM with both main and aux banks initialized to zero
   */
  RAM();

  /**
   * Destructor
   */
  ~RAM() override = default;

  // Delete copy constructor and assignment (non-copyable)
  RAM(const RAM &) = delete;
  RAM &operator=(const RAM &) = delete;

  // Allow move constructor and assignment
  RAM(RAM &&) = default;
  RAM &operator=(RAM &&) = default;

  /**
   * Read a byte from RAM
   * @param address 16-bit address
   * @return byte value from the currently selected read bank
   */
  uint8_t read(uint16_t address) override;

  /**
   * Write a byte to RAM
   * @param address 16-bit address
   * @param value byte value to write to the currently selected write bank
   */
  void write(uint16_t address, uint8_t value) override;

  /**
   * Get the address range this RAM occupies
   * @return AddressRange covering $0000-$BFFF
   */
  AddressRange getAddressRange() const override;

  /**
   * Get the name of this device
   * @return "RAM"
   */
  std::string getName() const override;

  /**
   * Set the active read bank
   * @param bank true for aux bank, false for main bank
   */
  void setReadBank(bool aux_bank);

  /**
   * Set the active write bank
   * @param bank true for aux bank, false for main bank
   */
  void setWriteBank(bool aux_bank);

  /**
   * Get direct access to main bank memory (for MMU/video access)
   * @return reference to main bank array
   */
  std::array<uint8_t, Apple2e::RAM_SIZE> &getMainBank() { return main_bank_; }

  /**
   * Get direct access to aux bank memory (for MMU/video access)
   * @return reference to aux bank array
   */
  std::array<uint8_t, Apple2e::RAM_SIZE> &getAuxBank() { return aux_bank_; }

  /**
   * Get const access to main bank memory
   * @return const reference to main bank array
   */
  const std::array<uint8_t, Apple2e::RAM_SIZE> &getMainBank() const { return main_bank_; }

  /**
   * Get const access to aux bank memory
   * @return const reference to aux bank array
   */
  const std::array<uint8_t, Apple2e::RAM_SIZE> &getAuxBank() const { return aux_bank_; }

private:
  std::array<uint8_t, Apple2e::RAM_SIZE> main_bank_;
  std::array<uint8_t, Apple2e::RAM_SIZE> aux_bank_;
  bool read_aux_bank_ = false;
  bool write_aux_bank_ = false;
};

