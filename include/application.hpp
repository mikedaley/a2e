#pragma once

#include <MOS6502/CPU6502.hpp>
#include <window/window_renderer.hpp>
#include <windows/cpu_window.hpp>
#include <windows/status_window.hpp>
#include <array>
#include <memory>
#include <functional>
#include <cstdint>
#include <vector>

/**
 * application - Main application class that manages the emulator state and UI
 *
 * This class follows the single responsibility principle and encapsulates
 * all application logic, separating it from the window management layer.
 */
class application
{
public:
  /**
   * Constructs the application
   */
  application();

  /**
   * Destructor
   */
  ~application();

  // Delete copy constructor and assignment (non-copyable)
  application(const application &) = delete;
  application &operator=(const application &) = delete;

  // Allow move constructor and assignment
  application(application &&) = default;
  application &operator=(application &&) = default;

  /**
   * Initialize the application
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * Run the application main loop
   * @return Exit code (0 on success)
   */
  int run();

private:
  /**
   * Simple memory implementation for the emulator
   */
  class memory
  {
  public:
    memory() { memory_.fill(0); }

    uint8_t read(uint16_t address) const { return memory_[address]; }
    void write(uint16_t address, uint8_t value) { memory_[address] = value; }

  private:
    std::array<uint8_t, 65536> memory_;
  };

  /**
   * Render callback for IMGUI
   */
  void renderUI();

  /**
   * Update callback (called each frame)
   */
  void update(float deltaTime);

  /**
   * Setup IMGUI windows and UI
   */
  void setupUI();

  /**
   * Render menu bar
   */
  void renderMenuBar();

  /**
   * Update CPU window state
   */
  void updateCPUWindow();

  // Forward declaration to avoid template complexity in header
  class cpu_wrapper;

  std::unique_ptr<window_renderer> window_renderer_;
  std::unique_ptr<memory> memory_;
  std::unique_ptr<cpu_wrapper> cpu_;

  // Windows
  std::unique_ptr<cpu_window> cpu_window_;
  std::unique_ptr<status_window> status_window_;

  bool should_close_ = false;
};
