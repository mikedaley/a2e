#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <functional>

// Forward declaration
struct MemoryEditor;

/**
 * Memory Viewer Window
 *
 * Displays memory contents using imgui_memory_editor.
 * Allows viewing and editing the full 64KB address space.
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
  ~memory_viewer_window() override;

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
   * Set the memory write callback
   * @param callback Function that writes a byte to an address
   */
  void setMemoryWriteCallback(std::function<void(uint16_t, uint8_t)> callback);

  /**
   * Set the base address to display (jumps to this address)
   * @param address Starting address
   */
  void setBaseAddress(uint16_t address);

private:
  std::function<uint8_t(uint16_t)> memory_read_callback_;
  std::function<void(uint16_t, uint8_t)> memory_write_callback_;
  std::unique_ptr<MemoryEditor> mem_edit_;
};
