#pragma once

#include <MOS6502/CPU6502.hpp>
#include "emulator/bus.hpp"
#include "emulator/ram.hpp"
#include "emulator/rom.hpp"
#include "emulator/mmu.hpp"
#include "emulator/keyboard.hpp"
#include "emulator/speaker.hpp"
#include "emulator/video_display.hpp"
#include "emulator/disk2.hpp"
#include "emulator/breakpoint_manager.hpp"
#include "apple2e/soft_switches.hpp"
#include <memory>
#include <functional>
#include <cstdint>

/**
 * execution_state - Emulator execution state for debugging
 */
enum class execution_state
{
  RUNNING,    // Normal continuous execution
  PAUSED,     // Execution paused (debugger stopped)
  STEP_OVER,  // Execute one instruction then pause
  STEP_OUT    // Continue until RTS/RTI then pause
};

/**
 * emulator - Encapsulates all Apple IIe emulation components and logic
 *
 * This class manages the core emulation: CPU, RAM, ROM, MMU, keyboard, speaker,
 * and video display. It handles the execution loop and provides access to
 * emulator state for UI components.
 */
class emulator
{
public:
  /**
   * CPU state structure for external access (UI display)
   */
  struct cpu_state
  {
    uint16_t pc = 0;
    uint8_t sp = 0;
    uint8_t p = 0;
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint64_t total_cycles = 0;
    bool initialized = false;
  };

  /**
   * Constructs the emulator
   */
  emulator();

  /**
   * Destructor
   */
  ~emulator();

  // Delete copy constructor and assignment (non-copyable)
  emulator(const emulator &) = delete;
  emulator &operator=(const emulator &) = delete;

  // Allow move constructor and assignment
  emulator(emulator &&) = default;
  emulator &operator=(emulator &&) = default;

  /**
   * Initialize the emulator components
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * Update the emulator state using audio-driven timing
   * Runs CPU cycles based on audio buffer fill level to keep audio in sync
   */
  void update();

  /**
   * Hard reset - simulate power cycle (cold boot)
   * Clears RAM and resets all soft switches
   */
  void reset();

  /**
   * Warm reset - jump to BASIC prompt without memory clear
   * Simulates pressing Ctrl+Reset
   */
  void warmReset();

  /**
   * Get current CPU state for display
   */
  cpu_state getCPUState() const;

  /**
   * Read memory (through MMU, may trigger soft switches)
   */
  uint8_t readMemory(uint16_t address) const;

  /**
   * Peek memory (through MMU, no side effects)
   */
  uint8_t peekMemory(uint16_t address) const;

  /**
   * Write memory (through MMU)
   */
  void writeMemory(uint16_t address, uint8_t value);

  /**
   * Get soft switch state (for display, no side effects)
   */
  Apple2e::SoftSwitchState getSoftSwitchState() const;

  /**
   * Get soft switch state including diagnostic values like CSW/KSW
   */
  Apple2e::SoftSwitchState getSoftSwitchSnapshot() const;

  /**
   * Get mutable soft switch state (for UI controls like alt charset toggle)
   */
  Apple2e::SoftSwitchState& getMutableSoftSwitchState();

  /**
   * Press a key on the keyboard
   */
  void keyDown(uint8_t key_code);

  /**
   * Check if keyboard has a pending key (strobe is set)
   */
  bool isKeyboardStrobeSet() const;

  /**
   * Check if speaker is initialized
   */
  bool isSpeakerInitialized() const;

  /**
   * Check if speaker is muted
   */
  bool isSpeakerMuted() const;

  /**
   * Set speaker mute state
   */
  void setSpeakerMuted(bool muted);

  /**
   * Get speaker volume
   */
  float getSpeakerVolume() const;

  /**
   * Set speaker volume
   */
  void setSpeakerVolume(float volume);

  /**
   * Reset speaker timing (call when regaining focus)
   */
  void resetSpeakerTiming();

  /**
   * Get speaker buffer fill percentage (for audio-driven timing)
   */
  float getSpeakerBufferFill() const;

  /**
   * Get video display for texture access
   */
  video_display* getVideoDisplay() { return video_display_.get(); }

  /**
   * Get main RAM bank for direct video access
   */
  const std::array<uint8_t, 65536>& getMainRAM() const;

  /**
   * Get auxiliary RAM bank for 80-column mode
   */
  const std::array<uint8_t, 65536>& getAuxRAM() const;

  /**
   * Initialize video display texture with Metal device
   * @param device Metal device for texture creation
   */
  void initializeVideoTexture(void* device);

  /**
   * Load character ROM for video display
   * @param path Path to character ROM file
   * @return true on success
   */
  bool loadCharacterROM(const std::string& path);

  /**
   * Insert a disk into a drive
   * @param drive Drive number (0 or 1)
   * @param filepath Path to disk image file
   * @return true on success
   */
  bool insertDisk(int drive, const std::string& filepath);

  /**
   * Eject the disk from a drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  /**
   * Check if a disk is inserted
   * @param drive Drive number (0 or 1)
   */
  bool isDiskInserted(int drive) const;

  /**
   * Get the Disk II controller for UI access
   */
  DiskII* getDiskII() { return disk_ii_.get(); }
  const DiskII* getDiskII() const { return disk_ii_.get(); }

  /**
   * Save emulator state to file
   * @param path Path to save file
   * @return true on success
   */
  bool saveState(const std::string& path);

  /**
   * Load emulator state from file
   * @param path Path to save file
   * @return true on success
   */
  bool loadState(const std::string& path);

  /**
   * Check if a saved state file exists
   * @param path Path to save file
   * @return true if file exists
   */
  static bool savedStateExists(const std::string& path);

  /**
   * Pause emulator execution (for debugging)
   */
  void pause();

  /**
   * Resume emulator execution
   */
  void resume();

  /**
   * Execute one instruction then pause
   */
  void stepOver();

  /**
   * Continue execution until returning from subroutine
   */
  void stepOut();

  /**
   * Check if emulator is paused
   * @return true if paused
   */
  bool isPaused() const;

  /**
   * Get current execution state
   * @return Current execution state
   */
  execution_state getExecutionState() const;

  /**
   * Get breakpoint manager for debugger UI
   * @return Pointer to breakpoint manager
   */
  breakpoint_manager* getBreakpointManager();

private:
  // Forward declaration to avoid template complexity in header
  class cpu_wrapper;

  // Core emulator components
  std::unique_ptr<Bus> bus_;
  std::unique_ptr<RAM> ram_;
  std::unique_ptr<ROM> rom_;
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<Keyboard> keyboard_;
  std::unique_ptr<Speaker> speaker_;
  std::unique_ptr<video_display> video_display_;
  std::unique_ptr<DiskII> disk_ii_;
  std::unique_ptr<cpu_wrapper> cpu_;

  bool first_update_ = true; // Track first update to sync speaker timing

  // Debugger state
  execution_state exec_state_ = execution_state::RUNNING;
  std::unique_ptr<breakpoint_manager> breakpoint_mgr_;
  uint8_t step_out_stack_depth_ = 0;
};
