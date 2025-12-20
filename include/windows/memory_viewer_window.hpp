#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <array>
#include <functional>

/**
 * Memory Viewer Window
 *
 * Displays a hex dump of memory contents with ASCII representation.
 * Allows navigation through the full 64KB address space.
 */
class memory_viewer_window : public base_window
{
public:
  /**
   * Constructor
   */
  memory_viewer_window();

  /**
   * Destructor
   */
  ~memory_viewer_window() override = default;

  /**
   * Render the memory viewer window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Memory Viewer"; }

  /**
   * Set the memory read callback
   * @param callback Function that reads a byte from an address
   */
  void setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback);

  /**
   * Set the base address to display
   * @param address Starting address (will be aligned to 16-byte boundary)
   */
  void setBaseAddress(uint16_t address);

  /**
   * Get the current base address
   * @return Current base address
   */
  uint16_t getBaseAddress() const { return base_address_; }

private:
  /**
   * Render the hex dump
   */
  void renderHexDump();

  /**
   * Render the address input controls
   */
  void renderControls();

  std::function<uint8_t(uint16_t)> memory_read_callback_;
  uint16_t base_address_ = 0x0000;
  int rows_to_display_ = 32;
  char address_input_[5] = "0000"; // Hex address input
  bool follow_pc_ = false;
  uint16_t highlight_address_ = 0xFFFF; // Address to highlight (0xFFFF = none)
};
