#pragma once

#include "base_window.hpp"
#include "apple2e/soft_switches.hpp"
#include <functional>

// Forward declaration
class emulator;

/**
 * Soft Switches Window
 *
 * Displays the current state of all Apple IIe soft switches.
 * This is a read-only debug view that does not affect emulator state.
 */
class soft_switches_window : public base_window
{
public:
  /**
   * Constructor
   * @param emu Reference to the emulator for accessing soft switch state
   */
  explicit soft_switches_window(emulator& emu);

  /**
   * Destructor
   */
  ~soft_switches_window() override = default;

  /**
   * Render the window
   */
  void render() override;

  /**
   * Get the window name
   */
  const char *getName() const override { return "Soft Switches"; }

private:
  /**
   * Render a switch indicator with label
   * @param label Display label for the switch
   * @param state Current state (true = on/active)
   * @param on_text Text to show when on (default "ON")
   * @param off_text Text to show when off (default "OFF")
   */
  void renderSwitch(const char *label, bool state, 
                    const char *on_text = "ON", const char *off_text = "OFF");

  /**
   * Render a section header
   * @param label Section title
   */
  void renderSectionHeader(const char *label);

  std::function<Apple2e::SoftSwitchState()> state_callback_;
};
