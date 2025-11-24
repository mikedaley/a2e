#pragma once

#include "base_window.hpp"

// Forward declaration
struct ImGuiIO;

/**
 * status_window - Displays application status and performance information
 *
 * Shows:
 * - Application version
 * - Emulator status
 * - Frame rate (FPS)
 * - Frame time
 */
class status_window : public base_window
{
public:
  /**
   * Constructs a status window
   * @param io_ptr Pointer to ImGuiIO for reading performance metrics
   */
  explicit status_window(ImGuiIO *io_ptr);

  /**
   * Render the status window
   */
  void render() override;

  /**
   * Get window name
   */
  const char *getName() const override { return "Status"; }

private:
  ImGuiIO *io_ptr_;
};
