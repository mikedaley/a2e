#pragma once

// Forward declaration
class preferences;

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
   * Update the window state (called each frame before render)
   * Override this to update window-specific state.
   * Default implementation does nothing.
   * @param deltaTime Time elapsed since last frame in seconds
   */
  virtual void update([[maybe_unused]] float deltaTime) {}

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

  /**
   * Load window-specific state from preferences (section visibility, etc.)
   * Override in subclasses that have internal state to persist.
   */
  virtual void loadState([[maybe_unused]] preferences& prefs) {}

  /**
   * Save window-specific state to preferences (section visibility, etc.)
   * Override in subclasses that have internal state to persist.
   */
  virtual void saveState([[maybe_unused]] preferences& prefs) {}

protected:
  bool open_ = true;
};
