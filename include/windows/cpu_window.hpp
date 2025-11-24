#pragma once

#include "base_window.hpp"
#include <cstdint>
#include <memory>

// Forward declaration to avoid exposing CPU implementation details
class application;

/**
 * cpu_window - Displays CPU register information
 *
 * Shows the current state of the 65C02 CPU registers including:
 * - Program Counter (PC)
 * - Stack Pointer (SP)
 * - Status Register (P)
 * - Accumulator (A)
 * - X Register
 * - Y Register
 */
class cpu_window : public base_window
{
public:
  /**
   * CPU state structure for rendering
   */
  struct cpu_state
  {
    uint16_t pc = 0; // Program Counter
    uint8_t sp = 0;  // Stack Pointer
    uint8_t p = 0;   // Status Register
    uint8_t a = 0;   // Accumulator
    uint8_t x = 0;   // X Register
    uint8_t y = 0;   // Y Register
    bool initialized = false;
  };

  /**
   * Constructs a CPU window
   */
  cpu_window();

  /**
   * Render the CPU window
   */
  void render() override;

  /**
   * Get window name
   */
  const char *getName() const override { return "CPU Registers"; }

  /**
   * Update the CPU state to display
   * @param state Current CPU state
   */
  void setCPUState(const cpu_state &state);

private:
  cpu_state state_;
};
