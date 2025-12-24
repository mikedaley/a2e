#pragma once

#include "ui/base_window.hpp"
#include <functional>
#include <vector>
#include <string>
#include <cstdint>

class emulator;
class breakpoint_manager;
enum class breakpoint_type;

/**
 * debugger_window - Interactive debugger with disassembly and breakpoints
 *
 * Provides a full debugging environment with:
 * - Disassembly view that follows the program counter
 * - Execution controls (Step Over, Run/Pause, Step Out)
 * - Breakpoint management (execution, read, write watchpoints)
 * - Visual indicators for current PC and breakpoints
 */
class debugger_window : public base_window
{
public:
  /**
   * Construct debugger window with emulator reference
   * @param emu Reference to emulator
   */
  explicit debugger_window(emulator& emu);

  /**
   * Update debugger state each frame
   * @param deltaTime Time since last frame
   */
  void update(float deltaTime) override;

  /**
   * Render debugger UI
   */
  void render() override;

  /**
   * Get window name
   * @return Window title
   */
  const char* getName() const override { return "Debugger"; }

private:
  // Callbacks to emulator
  std::function<uint16_t()> get_pc_callback_;
  std::function<uint8_t(uint16_t)> memory_read_callback_;
  std::function<void()> step_over_callback_;
  std::function<void()> step_out_callback_;
  std::function<void()> run_callback_;
  std::function<void()> pause_callback_;
  std::function<bool()> is_paused_callback_;
  std::function<breakpoint_manager*()> get_breakpoint_mgr_callback_;

  // UI state
  bool auto_follow_pc_ = true;
  int disasm_lines_ = 30;
  uint16_t current_pc_ = 0;
  uint16_t scroll_to_address_ = 0;
  bool scroll_pending_ = false;

  // Disassembly cache
  struct disasm_line
  {
    uint16_t address;
    uint8_t bytes[3];
    int byte_count;
    std::string instruction;
    bool is_current_pc;
    bool has_exec_breakpoint;
    bool has_read_breakpoint;
    bool has_write_breakpoint;
  };
  std::vector<disasm_line> disasm_cache_;
  uint16_t cache_center_address_ = 0;

  // Rendering methods
  void renderControls();
  void renderDisassembly();
  void renderBreakpoints();

  // Helper methods
  void updateDisassemblyCache(uint16_t center_address);
  void handleBreakpointClick(uint16_t address, breakpoint_type type);
};
