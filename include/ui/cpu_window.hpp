#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <array>
#include <string>
#include <functional>

// Forward declaration
class emulator;

/**
 * cpu_window - Advanced CPU debugging and visualization window
 *
 * Displays comprehensive information about the 65C02 CPU including:
 * - All registers with multiple display formats (hex/dec/bin)
 * - Individual status flag visualization
 * - Stack memory preview
 * - Disassembly at current PC
 * - Cycle counter and performance metrics
 * - Memory preview at PC location
 */
class cpu_window : public base_window
{
public:
  /**
   * CPU state structure with all available information
   */
  struct cpu_state
  {
    // Core registers
    uint16_t pc = 0;     // Program Counter
    uint8_t sp = 0;      // Stack Pointer
    uint8_t p = 0;       // Status Register (flags)
    uint8_t a = 0;       // Accumulator
    uint8_t x = 0;       // X Index Register
    uint8_t y = 0;       // Y Index Register

    // Cycle information
    uint64_t total_cycles = 0;
    uint64_t instructions_executed = 0;

    // Stack contents (top 16 bytes)
    std::array<uint8_t, 16> stack_preview = {};

    // Memory at PC (for disassembly, 16 bytes)
    std::array<uint8_t, 16> memory_at_pc = {};

    // Disassembled instruction at PC
    std::string current_instruction;

    // Previous values for change detection
    uint16_t prev_pc = 0;
    uint8_t prev_sp = 0;
    uint8_t prev_p = 0;
    uint8_t prev_a = 0;
    uint8_t prev_x = 0;
    uint8_t prev_y = 0;

    bool initialized = false;
  };

  /**
   * Constructs a CPU window
   * @param emu Reference to the emulator for accessing CPU state
   */
  explicit cpu_window(emulator& emu);

  /**
   * Update the window state (fetches CPU state from emulator)
   * @param deltaTime Time elapsed since last frame in seconds
   */
  void update(float deltaTime) override;

  /**
   * Render the CPU window
   */
  void render() override;

  /**
   * Get window name
   */
  const char *getName() const override { return "CPU Monitor"; }

private:
  cpu_state state_;
  cpu_state prev_state_;
  std::function<cpu_state()> cpu_state_callback_;
  std::function<uint8_t(uint16_t)> memory_read_callback_;

  // UI state
  bool show_binary_ = true;
  bool show_decimal_ = true;
  bool show_stack_ = false;
  bool show_disasm_ = false;
  int disasm_lines_ = 10;

  // Performance tracking
  uint64_t last_cycle_count_ = 0;
  float cycles_per_second_ = 0.0f;
  float last_update_time_ = 0.0f;

  // Helper methods
  void renderRegisters();
  void renderStatusFlags();
  void renderStack();
  void renderDisassembly();
  void renderPerformance();

  // Format helpers
  static std::string formatBinary8(uint8_t value);
  static std::string formatBinary16(uint16_t value);

  // Disassembly helper
  std::string disassembleInstruction(uint16_t address);
};
