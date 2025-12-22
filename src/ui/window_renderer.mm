#include "ui/window_renderer.hpp"
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <cstdlib>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>
#include <imgui_impl_metal.h>
#include <SDL3/SDL_properties.h>

window_renderer::window_renderer(const config &config)
    : config_(config)
{
  try
  {
    initSDL();
    createWindow();
    setupMetal();
    initImGui();
    initialized_ = true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "window_renderer initialization failed: " << e.what() << std::endl;
    // Cleanup any partially initialized resources
    render_pass_descriptor_ = nullptr;
    command_queue_ = nullptr;
    metal_device_ = nullptr;
    if (metal_view_)
    {
      SDL_Metal_DestroyView(metal_view_);
      metal_view_ = nullptr;
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

window_renderer::~window_renderer()
{
  shutdownImGui();

  // Note: We don't need to manually release Metal objects created with
  // MTLCreateSystemDefaultDevice(), newCommandQueue, and [MTLRenderPassDescriptor new]
  // because we're using __bridge which doesn't transfer ownership.
  // The objects are autoreleased or managed by ARC.
  
  render_pass_descriptor_ = nullptr;
  command_queue_ = nullptr;
  metal_device_ = nullptr;

  if (metal_view_)
  {
    SDL_Metal_DestroyView(metal_view_);
    metal_view_ = nullptr;
  }

  if (window_)
  {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}

window_renderer::window_renderer(window_renderer &&other) noexcept
    : config_(other.config_), window_(other.window_), metal_view_(other.metal_view_),
      metal_device_(other.metal_device_), command_queue_(other.command_queue_),
      render_pass_descriptor_(other.render_pass_descriptor_),
      display_scale_(other.display_scale_), should_close_(other.should_close_),
      initialized_(other.initialized_)
{
  other.window_ = nullptr;
  other.metal_view_ = nullptr;
  other.metal_device_ = nullptr;
  other.command_queue_ = nullptr;
  other.render_pass_descriptor_ = nullptr;
  other.initialized_ = false;
}

window_renderer &window_renderer::operator=(window_renderer &&other) noexcept
{
  if (this != &other)
  {
    // Cleanup current resources
    shutdownImGui();
    render_pass_descriptor_ = nullptr;
    command_queue_ = nullptr;
    metal_device_ = nullptr;
    if (metal_view_)
    {
      SDL_Metal_DestroyView(metal_view_);
    }
    if (window_)
    {
      SDL_DestroyWindow(window_);
    }

    // Move resources
    config_ = other.config_;
    window_ = other.window_;
    metal_view_ = other.metal_view_;
    metal_device_ = other.metal_device_;
    command_queue_ = other.command_queue_;
    render_pass_descriptor_ = other.render_pass_descriptor_;
    display_scale_ = other.display_scale_;
    should_close_ = other.should_close_;
    initialized_ = other.initialized_;

    // Reset other
    other.window_ = nullptr;
    other.metal_view_ = nullptr;
    other.metal_device_ = nullptr;
    other.command_queue_ = nullptr;
    other.render_pass_descriptor_ = nullptr;
    other.initialized_ = false;
  }
  return *this;
}

void window_renderer::initSDL()
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
  {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }
}

void window_renderer::createWindow()
{
  // Get display scale for DPI-aware rendering
  display_scale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

  // Calculate scaled window size
  int scaled_width = static_cast<int>(config_.width * display_scale_);
  int scaled_height = static_cast<int>(config_.height * display_scale_);

  // Create window flags for Metal
  SDL_WindowFlags window_flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

  // Create window
  window_ = SDL_CreateWindow(config_.title.c_str(), scaled_width, scaled_height, window_flags);
  if (window_ == nullptr)
  {
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }

  // Center and show window
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window_);
}

void window_renderer::setupMetal()
{
  // Create Metal device
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (!device)
  {
    throw std::runtime_error("Failed to create Metal device");
  }
  // MTLCreateSystemDefaultDevice returns a retained object (+1), no need to retain again
  metal_device_ = (__bridge void *)device;

  // Create Metal view
  metal_view_ = SDL_Metal_CreateView(window_);
  if (!metal_view_)
  {
    throw std::runtime_error(std::string("SDL_Metal_CreateView failed: ") + SDL_GetError());
  }

  // Configure Metal layer (match ImGui SDL3+Metal example)
  CAMetalLayer *layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(metal_view_);
  layer.device = device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

  // Enable VSync to sync with display refresh rate (60Hz, 120Hz, ProMotion, etc.)
  layer.displaySyncEnabled = YES;

  // Allow variable refresh rates on ProMotion displays (macOS 12.0+)
  if (@available(macOS 12.0, *)) {
    // Try to set variable refresh rate behavior
    // This selector may not exist on all systems, so check first
    if ([layer respondsToSelector:@selector(setDisplaySyncEnabledVariableRefreshRate:)]) {
      // Use performSelector to avoid compiler warnings about unknown selector
      SEL selector = NSSelectorFromString(@"setDisplaySyncEnabledVariableRefreshRate:");
      NSMethodSignature *signature = [layer methodSignatureForSelector:selector];
      if (signature) {
        NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
        [invocation setSelector:selector];
        [invocation setTarget:layer];
        BOOL value = YES;
        [invocation setArgument:&value atIndex:2];
        [invocation invoke];
      }
    }
  }

  // Create command queue
  id<MTLCommandQueue> queue = [device newCommandQueue];
  if (!queue)
  {
    throw std::runtime_error("Failed to create Metal command queue");
  }
  // newCommandQueue already returns a retained object
  command_queue_ = (__bridge void *)queue;

  // Create render pass descriptor
  MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor new];
  rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
  rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
  rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
  // new already returns a retained object
  render_pass_descriptor_ = (__bridge void *)rpd;
}

// Static storage for ImGui ini file path (must persist for lifetime of ImGui)
static std::string s_imgui_ini_path;

void window_renderer::initImGui()
{
  // Initialize IMGUI
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();

  // Set up ini file path in user's config directory for window state persistence
  const char* home = std::getenv("HOME");
  if (home)
  {
    std::filesystem::path config_dir = std::filesystem::path(home) / ".config" / "a2e";
    try
    {
      std::filesystem::create_directories(config_dir);
      s_imgui_ini_path = (config_dir / "imgui.ini").string();
      io.IniFilename = s_imgui_ini_path.c_str();
    }
    catch (const std::exception& e)
    {
      std::cerr << "Failed to create config directory: " << e.what() << std::endl;
      // Fall back to default (current directory)
    }
  }

  // Enable keyboard and gamepad navigation
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Enable docking and viewports (available in docking branch)
  if (config_.docking)
  {
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  }

  if (config_.viewports)
  {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  }

  // Setup style
  ImGui::StyleColorsDark();

  // Apply display scaling
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(display_scale_);
  style.FontScaleDpi = display_scale_;

  // Initialize platform and renderer backends
  // IMPORTANT: Initialize SDL3 backend BEFORE Metal backend
  ImGui_ImplSDL3_InitForMetal(window_);
  id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
  ImGui_ImplMetal_Init(device);
}

void window_renderer::shutdownImGui()
{
  if (initialized_)
  {
    // IMPORTANT: Shutdown in reverse order of initialization
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
}

int window_renderer::run(RenderCallback renderCallback, UpdateCallback updateCallback)
{
  if (!initialized_)
  {
    std::cerr << "window_renderer not initialized" << std::endl;
    return 1;
  }

  if (!renderCallback)
  {
    std::cerr << "Render callback is required" << std::endl;
    return 1;
  }

  // Store callbacks for use in event watch during live-resize
  render_callback_ = renderCallback;
  update_callback_ = updateCallback;

  // Add event watch to handle rendering during live-resize on macOS
  SDL_AddEventWatch(LiveResizeEventWatch, this);

  // Timing for 50Hz frame rate (20ms per frame)
  constexpr std::chrono::duration<double> FRAME_DURATION(1.0 / 50.0);  // 20ms
  auto last_time = std::chrono::high_resolution_clock::now();
  auto frame_start = last_time;

  // Main loop
  while (!should_close_)
  {
    @autoreleasepool
    {
      frame_start = std::chrono::high_resolution_clock::now();

      // Calculate delta time
      auto delta_time = std::chrono::duration<float>(frame_start - last_time).count();
      last_time = frame_start;

      // Process events
      if (!processEvents())
      {
        break;
      }

      // Skip rendering if window is minimized
      SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
      if (flags & SDL_WINDOW_MINIMIZED)
      {
        SDL_Delay(10);
        continue;
      }

      // Check if event watch just rendered (during live resize)
      if (rendered_from_event_watch_.exchange(false))
      {
        // Event watch already rendered this frame, skip main loop render
        continue;
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

      // Frame rate limiting to 50Hz
      auto frame_end = std::chrono::high_resolution_clock::now();
      auto frame_elapsed = frame_end - frame_start;
      if (frame_elapsed < FRAME_DURATION)
      {
        auto sleep_time = FRAME_DURATION - frame_elapsed;
        std::this_thread::sleep_for(sleep_time);
      }
    }
  }

  // Remove event watch
  SDL_RemoveEventWatch(LiveResizeEventWatch, this);
  render_callback_ = nullptr;
  update_callback_ = nullptr;

  return 0;
}

bool window_renderer::processEvents()
{
  SDL_Event event;

  // Use SDL_PollEvent to process all queued events
  // During live resize, SDL3's NSTimer pumps events including SDL_EVENT_WINDOW_EXPOSED
  while (SDL_PollEvent(&event))
  {
    // Pass events to IMGUI first
    ImGui_ImplSDL3_ProcessEvent(&event);

    // Handle quit events
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
      should_close_ = true;
      return false;
    }
  }

  return true;
}

void window_renderer::beginFrame()
{
  // Get window size in pixels EVERY frame (critical for smooth resize)
  int width, height;
  SDL_GetWindowSizeInPixels(window_, &width, &height);

  // Get Metal layer
  CAMetalLayer *layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(metal_view_);

  // CRITICAL: Update drawable size EVERY frame, BEFORE getting drawable
  // This is how the official ImGui SDL3+Metal example does it
  layer.drawableSize = CGSizeMake(width, height);

  // Also ensure the layer's contentsScale matches the display scale
  // This prevents scaling artifacts during resize
  float current_scale = SDL_GetWindowDisplayScale(window_);
  if (layer.contentsScale != current_scale) {
    layer.contentsScale = current_scale;
  }

  // Get current drawable (must be done AFTER setting drawableSize)
  id<CAMetalDrawable> drawable = [layer nextDrawable];
  if (!drawable)
  {
    std::cerr << "WARNING: nextDrawable returned nil!" << std::endl;
    return; // Skip frame if no drawable available
  }
  // Store drawable for use in endFrame
  current_drawable_ = (__bridge void *)drawable;

  // Update render pass descriptor with drawable texture
  // This must be done before ImGui_ImplMetal_NewFrame so it can read sampleCount
  MTLRenderPassDescriptor *rpd = (__bridge MTLRenderPassDescriptor *)render_pass_descriptor_;
  rpd.colorAttachments[0].texture = drawable.texture;
  rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
  rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
  rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

  // Start the IMGUI frame (now that render pass descriptor has texture with valid sampleCount)
  ImGui_ImplMetal_NewFrame(rpd);
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void window_renderer::endFrame()
{
  // Check if we have valid drawable from beginFrame
  if (!current_drawable_)
  {
    return; // beginFrame failed, skip rendering
  }

  // Render IMGUI
  ImGui::Render();
  ImDrawData *draw_data = ImGui::GetDrawData();

  if (!draw_data || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
  {
    // Clean up and return
    current_drawable_ = nullptr;
    return; // Nothing to render
  }

  // Get stored drawable
  id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)current_drawable_;

  // Get render pass descriptor
  MTLRenderPassDescriptor *rpd = (__bridge MTLRenderPassDescriptor *)render_pass_descriptor_;

  // Create command buffer
  id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
  id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
  if (!command_buffer)
  {
    current_drawable_ = nullptr;
    return;
  }

  // Create render command encoder
  id<MTLRenderCommandEncoder> render_encoder = [command_buffer renderCommandEncoderWithDescriptor:rpd];
  if (!render_encoder)
  {
    current_drawable_ = nullptr;
    return;
  }

  // Render IMGUI draw data
  ImGui_ImplMetal_RenderDrawData(draw_data, command_buffer, render_encoder);

  // End encoding
  [render_encoder endEncoding];

  // Present and commit (match ImGui SDL3+Metal example)
  [command_buffer presentDrawable:drawable];
  [command_buffer commit];

  // Clear stored reference
  current_drawable_ = nullptr;

// Update and render additional platform windows (if multi-viewport enabled)
// This is only available in the IMGUI docking branch
#ifdef IMGUI_HAS_DOCK
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
#endif
}

std::pair<int, int> window_renderer::getWindowSize() const
{
  if (!window_)
  {
    return {0, 0};
  }

  int width, height;
  SDL_GetWindowSize(window_, &width, &height);
  return {width, height};
}

std::pair<int, int> window_renderer::getWindowPosition() const
{
  if (!window_)
  {
    return {0, 0};
  }

  int x, y;
  SDL_GetWindowPosition(window_, &x, &y);
  return {x, y};
}

void window_renderer::setWindowGeometry(int x, int y, int width, int height)
{
  if (!window_)
  {
    return;
  }

  SDL_SetWindowPosition(window_, x, y);
  SDL_SetWindowSize(window_, width, height);
}

bool window_renderer::isMaximized() const
{
  if (!window_)
  {
    return false;
  }

  SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
  return (flags & SDL_WINDOW_MAXIMIZED) != 0;
}

void window_renderer::setMaximized(bool maximized)
{
  if (!window_)
  {
    return;
  }

  if (maximized)
  {
    SDL_MaximizeWindow(window_);
  }
  else
  {
    SDL_RestoreWindow(window_);
  }
}

bool window_renderer::hasFocus() const noexcept
{
  if (!window_)
  {
    return false;
  }
  SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
  return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool window_renderer::LiveResizeEventWatch(void* userdata, SDL_Event* event)
{
  window_renderer* self = static_cast<window_renderer*>(userdata);
  if (!self) return true;

  if (event->type == SDL_EVENT_WINDOW_EXPOSED)
  {
    // Check if this is a live-resize exposed event (data1 will be non-zero)
    if (event->window.data1 != 0)
    {
      if (self->render_callback_)
      {
        // Render one frame during live-resize
        self->renderOneFrameLiveResize();
      }
    }
  }

  return true; // Return true to continue normal event processing
}

void window_renderer::renderOneFrameLiveResize()
{
  @autoreleasepool
  {
    // Skip rendering if window is minimized
    SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    if (flags & SDL_WINDOW_MINIMIZED)
    {
      return;
    }

    // Mark that we're rendering from event watch
    rendered_from_event_watch_.store(true);

    // Begin IMGUI frame
    beginFrame();

    // Render callback
    if (render_callback_)
    {
      render_callback_();
    }

    // End IMGUI frame
    endFrame();
  }
}
