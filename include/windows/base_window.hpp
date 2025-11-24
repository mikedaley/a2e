#pragma once

/**
 * base_window - Interface for all IMGUI windows in the application
 *
 * This interface provides a common structure for all windows,
 * allowing them to be managed uniformly by the application.
 */
class base_window
{
public:
  virtual ~base_window() = default;

  /**
   * Render the window contents
   * Should call ImGui::Begin/End internally
   */
  virtual void render() = 0;

  /**
   * Get window name
   */
  virtual const char *getName() const = 0;

  /**
   * Check if window is currently open/visible
   */
  virtual bool isOpen() const { return open_; }

  /**
   * Set window open/closed state
   */
  virtual void setOpen(bool open) { open_ = open; }

protected:
  bool open_ = true;
};
