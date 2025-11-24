#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <functional>
#include <memory>
#include <string>

/**
 * WindowManager - Manages SDL3 window, OpenGL context, and IMGUI lifecycle
 *
 * This class follows RAII principles and modern C++ best practices:
 * - Automatic resource cleanup
 * - Move-only semantics (non-copyable)
 * - Exception-safe initialization
 * - Callback-based rendering architecture
 */
class WindowManager
{
public:
  using RenderCallback = std::function<void()>;
  using UpdateCallback = std::function<void(float deltaTime)>;

  /**
   * Window configuration structure
   */
  struct Config
  {
    std::string title = "Application";
    int width = 1280;
    int height = 800;
    bool vsync = true;
    bool docking = false;   // Enable IMGUI docking
    bool viewports = false; // Enable multi-viewport support
  };

  /**
   * Constructs and initializes the window manager
   * @param config Window configuration
   * @throws std::runtime_error if initialization fails
   */
  explicit WindowManager(const Config &config);

  /**
   * Destructor - automatically cleans up all resources
   */
  ~WindowManager();

  // Delete copy constructor and assignment (non-copyable)
  WindowManager(const WindowManager &) = delete;
  WindowManager &operator=(const WindowManager &) = delete;

  // Allow move constructor and assignment
  WindowManager(WindowManager &&other) noexcept;
  WindowManager &operator=(WindowManager &&other) noexcept;

  /**
   * Runs the main event loop
   * @param renderCallback Called each frame to render IMGUI content
   * @param updateCallback Optional callback for per-frame updates (deltaTime in seconds)
   * @return Exit code (0 on success)
   */
  int run(RenderCallback renderCallback, UpdateCallback updateCallback = nullptr);

  /**
   * Check if the window should close
   */
  [[nodiscard]] bool shouldClose() const noexcept { return should_close_; }

  /**
   * Request window to close
   */
  void close() noexcept { should_close_ = true; }

  /**
   * Get the SDL window handle
   */
  [[nodiscard]] SDL_Window *getWindow() const noexcept { return window_; }

  /**
   * Get the OpenGL context
   */
  [[nodiscard]] SDL_GLContext getGLContext() const noexcept { return gl_context_; }

  /**
   * Get IMGUI IO reference for configuration
   */
  [[nodiscard]] ImGuiIO &getIO() noexcept { return ImGui::GetIO(); }

  /**
   * Get the current display scale (for DPI-aware rendering)
   */
  [[nodiscard]] float getDisplayScale() const noexcept { return display_scale_; }

  /**
   * Get window dimensions
   */
  [[nodiscard]] std::pair<int, int> getWindowSize() const;

private:
  /**
   * Initialize SDL3 subsystems
   */
  void initSDL();

  /**
   * Setup OpenGL attributes and create context
   */
  void setupOpenGL();

  /**
   * Create SDL window
   */
  void createWindow();

  /**
   * Initialize IMGUI
   */
  void initImGui();

  /**
   * Cleanup IMGUI resources
   */
  void shutdownImGui();

  /**
   * Process SDL events
   * @return true if should continue, false if should quit
   */
  bool processEvents();

  /**
   * Begin IMGUI frame
   */
  void beginFrame();

  /**
   * End IMGUI frame and swap buffers
   */
  void endFrame();

  Config config_;
  SDL_Window *window_ = nullptr;
  SDL_GLContext gl_context_ = nullptr;
  float display_scale_ = 1.0f;
  bool should_close_ = false;
  bool initialized_ = false;
};
