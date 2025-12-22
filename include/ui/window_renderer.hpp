#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <functional>
#include <memory>
#include <string>

#ifdef __APPLE__
// Forward declarations for Objective-C types
// Metal headers are included in the .mm implementation file
#endif

/**
 * window_renderer - Manages SDL3 window, Metal rendering, and IMGUI lifecycle
 *
 * This class follows RAII principles and modern C++ best practices:
 * - Automatic resource cleanup
 * - Move-only semantics (non-copyable)
 * - Exception-safe initialization
 * - Callback-based rendering architecture
 */
class window_renderer
{
public:
  using RenderCallback = std::function<void()>;
  using UpdateCallback = std::function<void(float deltaTime)>;

  /**
   * Window configuration structure
   */
  struct config
  {
    std::string title = "application";
    int width = 1280;
    int height = 800;
    bool vsync = true;
    bool docking = true;    // Enable IMGUI docking
    bool viewports = false; // Enable multi-viewport support
  };

  /**
   * Constructs and initializes the window renderer
   * @param config Window configuration
   * @throws std::runtime_error if initialization fails
   */
  explicit window_renderer(const config &config);

  /**
   * Destructor - automatically cleans up all resources
   */
  ~window_renderer();

  // Delete copy constructor and assignment (non-copyable)
  window_renderer(const window_renderer &) = delete;
  window_renderer &operator=(const window_renderer &) = delete;

  // Allow move constructor and assignment
  window_renderer(window_renderer &&other) noexcept;
  window_renderer &operator=(window_renderer &&other) noexcept;

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
   * Get the Metal device
   */
#ifdef __APPLE__
  [[nodiscard]] void *getMetalDevice() const noexcept { return metal_device_; }
#else
  [[nodiscard]] void *getMetalDevice() const noexcept { return nullptr; }
#endif

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

  /**
   * Get window position
   */
  [[nodiscard]] std::pair<int, int> getWindowPosition() const;

  /**
   * Set window position and size
   * @param x X position
   * @param y Y position
   * @param width Window width
   * @param height Window height
   */
  void setWindowGeometry(int x, int y, int width, int height);

  /**
   * Check if window is maximized
   */
  [[nodiscard]] bool isMaximized() const;

  /**
   * Set window maximized state
   */
  void setMaximized(bool maximized);

  /**
   * Check if window has input focus
   */
  [[nodiscard]] bool hasFocus() const noexcept;

private:
  /**
   * Initialize SDL3 subsystems
   */
  void initSDL();

  /** Render one frame safely (used by live-resize event watch). */
  void renderOneFrameLiveResize();

  /** SDL event watch to render during live-resize on macOS. */
  static bool SDLCALL LiveResizeEventWatch(void *userdata, SDL_Event *event);

  /**
   * Setup Metal device and view
   */
  void setupMetal();

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

  config config_;
  SDL_Window *window_ = nullptr;
  SDL_MetalView metal_view_ = nullptr;

  // Store callbacks so we can render from event watch during live-resize
  RenderCallback render_callback_ = nullptr;
  UpdateCallback update_callback_ = nullptr;

  // Track if we just rendered from event watch to avoid double-rendering
  std::atomic<bool> rendered_from_event_watch_{false};
#ifdef __APPLE__
  void *metal_device_ = nullptr;           // id<MTLDevice>
  void *command_queue_ = nullptr;          // id<MTLCommandQueue>
  void *render_pass_descriptor_ = nullptr; // MTLRenderPassDescriptor*
  void *current_drawable_ = nullptr;       // id<CAMetalDrawable> - stored between beginFrame and endFrame
#else
  void *metal_device_ = nullptr;
  void *command_queue_ = nullptr;
  void *render_pass_descriptor_ = nullptr;
  void *current_drawable_ = nullptr;
#endif
  float display_scale_ = 1.0f;
  bool should_close_ = false;
  bool initialized_ = false;
};
