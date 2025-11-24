#pragma once

#include <MOS6502/CPU6502.hpp>
#include <window/WindowManager.hpp>
#include <array>
#include <memory>
#include <functional>
#include <cstdint>

/**
 * Application - Main application class that manages the emulator state and UI
 *
 * This class follows the single responsibility principle and encapsulates
 * all application logic, separating it from the window management layer.
 */
class Application
{
public:
  /**
   * Constructs the application
   */
  Application();

  /**
   * Destructor
   */
  ~Application();

  // Delete copy constructor and assignment (non-copyable)
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  // Allow move constructor and assignment
  Application(Application &&) = default;
  Application &operator=(Application &&) = default;

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
  class Memory
  {
  public:
    Memory() { memory_.fill(0); }

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
   * Render CPU registers window
   */
  void renderCPUWindow();

  /**
   * Render status window
   */
  void renderStatusWindow();

  // Forward declaration to avoid template complexity in header
  class CPUWrapper;

  std::unique_ptr<WindowManager> window_manager_;
  std::unique_ptr<Memory> memory_;
  std::unique_ptr<CPUWrapper> cpu_;

  bool should_close_ = false;
};
