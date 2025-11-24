#include "WindowManager.hpp"
#include <stdexcept>
#include <iostream>
#include <chrono>

// OpenGL includes - platform specific
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// Determine GLSL version based on platform
#if defined(IMGUI_IMPL_OPENGL_ES2)
#define GLSL_VERSION "#version 100"
#elif defined(IMGUI_IMPL_OPENGL_ES3)
#define GLSL_VERSION "#version 300 es"
#elif defined(__APPLE__)
#define GLSL_VERSION "#version 150"
#else
#define GLSL_VERSION "#version 130"
#endif

WindowManager::WindowManager(const Config &config)
    : config_(config)
{
  try
  {
    initSDL();
    setupOpenGL();
    createWindow();
    initImGui();
    initialized_ = true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "WindowManager initialization failed: " << e.what() << std::endl;
    // Cleanup any partially initialized resources
    if (gl_context_)
    {
      SDL_GL_DestroyContext(gl_context_);
      gl_context_ = nullptr;
    }
    if (window_)
    {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    SDL_Quit();
    throw;
  }
}

WindowManager::~WindowManager()
{
  shutdownImGui();

  if (gl_context_)
  {
    SDL_GL_DestroyContext(gl_context_);
    gl_context_ = nullptr;
  }

  if (window_)
  {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}

WindowManager::WindowManager(WindowManager &&other) noexcept
    : config_(other.config_), window_(other.window_), gl_context_(other.gl_context_), display_scale_(other.display_scale_), should_close_(other.should_close_), initialized_(other.initialized_)
{
  other.window_ = nullptr;
  other.gl_context_ = nullptr;
  other.initialized_ = false;
}

WindowManager &WindowManager::operator=(WindowManager &&other) noexcept
{
  if (this != &other)
  {
    // Cleanup current resources
    shutdownImGui();
    if (gl_context_)
    {
      SDL_GL_DestroyContext(gl_context_);
    }
    if (window_)
    {
      SDL_DestroyWindow(window_);
    }

    // Move resources
    config_ = other.config_;
    window_ = other.window_;
    gl_context_ = other.gl_context_;
    display_scale_ = other.display_scale_;
    should_close_ = other.should_close_;
    initialized_ = other.initialized_;

    // Reset other
    other.window_ = nullptr;
    other.gl_context_ = nullptr;
    other.initialized_ = false;
  }
  return *this;
}

void WindowManager::initSDL()
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
  {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }
}

void WindowManager::setupOpenGL()
{
  // Set OpenGL attributes based on platform
#if defined(IMGUI_IMPL_OPENGL_ES2)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
  // macOS requires OpenGL 3.2 Core profile
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  // Set additional OpenGL attributes
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
}

void WindowManager::createWindow()
{
  // Get display scale for DPI-aware rendering
  display_scale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

  // Calculate scaled window size
  int scaled_width = static_cast<int>(config_.width * display_scale_);
  int scaled_height = static_cast<int>(config_.height * display_scale_);

  // Create window flags
  SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

  // Create window
  window_ = SDL_CreateWindow(config_.title.c_str(), scaled_width, scaled_height, window_flags);
  if (window_ == nullptr)
  {
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }

  // Create OpenGL context
  gl_context_ = SDL_GL_CreateContext(window_);
  if (gl_context_ == nullptr)
  {
    throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
  }

  // Make context current
  SDL_GL_MakeCurrent(window_, gl_context_);

  // Enable vsync if requested
  SDL_GL_SetSwapInterval(config_.vsync ? 1 : 0);

  // Center and show window
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window_);
}

void WindowManager::initImGui()
{
  // Initialize IMGUI
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();

  // Enable keyboard and gamepad navigation
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Note: Docking and Viewports are only available in the IMGUI docking branch
  // These flags will be ignored if not available
  if (config_.docking)
  {
// Try to enable docking if available (docking branch only)
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
  }

  if (config_.viewports)
  {
// Try to enable viewports if available (docking branch only)
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
  }

  // Setup style
  ImGui::StyleColorsDark();

  // Apply display scaling
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(display_scale_);
  style.FontScaleDpi = display_scale_;

  // Initialize platform and renderer backends
  ImGui_ImplSDL3_InitForOpenGL(window_, gl_context_);
  ImGui_ImplOpenGL3_Init(GLSL_VERSION);
}

void WindowManager::shutdownImGui()
{
  if (initialized_)
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
}

int WindowManager::run(RenderCallback renderCallback, UpdateCallback updateCallback)
{
  if (!initialized_)
  {
    std::cerr << "WindowManager not initialized" << std::endl;
    return 1;
  }

  if (!renderCallback)
  {
    std::cerr << "Render callback is required" << std::endl;
    return 1;
  }

  // Timing for delta time calculation
  auto last_time = std::chrono::high_resolution_clock::now();

  // Main loop
  while (!should_close_)
  {
    // Calculate delta time
    auto current_time = std::chrono::high_resolution_clock::now();
    auto delta_time = std::chrono::duration<float>(current_time - last_time).count();
    last_time = current_time;

    // Process events
    if (!processEvents())
    {
      break;
    }

    // Update callback
    if (updateCallback)
    {
      updateCallback(delta_time);
    }

    // Begin IMGUI frame
    beginFrame();

    // Render callback
    renderCallback();

    // End IMGUI frame
    endFrame();
  }

  return 0;
}

bool WindowManager::processEvents()
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    // Pass events to IMGUI
    ImGui_ImplSDL3_ProcessEvent(&event);

    // Handle window close
    if (event.type == SDL_EVENT_QUIT)
    {
      should_close_ = true;
      return false;
    }

    // Handle window close request
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
      should_close_ = true;
      return false;
    }
  }

  return true;
}

void WindowManager::beginFrame()
{
  // Start the IMGUI frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void WindowManager::endFrame()
{
  // Render IMGUI
  ImGui::Render();

  // Get display size for viewport
  int display_w, display_h;
  SDL_GetWindowSize(window_, &display_w, &display_h);

  // Setup viewport
  glViewport(0, 0, display_w, display_h);

  // Clear framebuffer
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Render IMGUI draw data
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

// Update and render additional platform windows (if multi-viewport enabled)
// This is only available in the IMGUI docking branch
#ifdef IMGUI_HAS_DOCK
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    SDL_Window *backup_current_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
  }
#endif

  // Swap buffers
  SDL_GL_SwapWindow(window_);
}

std::pair<int, int> WindowManager::getWindowSize() const
{
  if (!window_)
  {
    return {0, 0};
  }

  int width, height;
  SDL_GetWindowSize(window_, &width, &height);
  return {width, height};
}
