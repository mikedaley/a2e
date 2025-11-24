#include <window/window_manager.hpp>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <atomic>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>
#include <imgui_impl_metal.h>
#include <SDL3/SDL_properties.h>

window_manager::window_manager(const config &config)
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
    std::cerr << "window_manager initialization failed: " << e.what() << std::endl;
    // Cleanup any partially initialized resources
    if (render_pass_descriptor_)
    {
      MTLRenderPassDescriptor *rpd = (__bridge MTLRenderPassDescriptor *)render_pass_descriptor_;
      [rpd release];
      render_pass_descriptor_ = nullptr;
    }
    if (command_queue_)
    {
      id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
      [queue release];
      command_queue_ = nullptr;
    }
    if (metal_device_)
    {
      id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
      [device release];
      metal_device_ = nullptr;
    }
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

window_manager::~window_manager()
{
  shutdownImGui();

  if (render_pass_descriptor_)
  {
    MTLRenderPassDescriptor *rpd = (__bridge MTLRenderPassDescriptor *)render_pass_descriptor_;
    [rpd release];
    render_pass_descriptor_ = nullptr;
  }

  if (command_queue_)
  {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
    [queue release];
    command_queue_ = nullptr;
  }

  if (metal_device_)
  {
    id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
    [device release];
    metal_device_ = nullptr;
  }

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

window_manager::window_manager(window_manager &&other) noexcept
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

window_manager &window_manager::operator=(window_manager &&other) noexcept
{
  if (this != &other)
  {
    // Cleanup current resources
    shutdownImGui();
    if (render_pass_descriptor_)
    {
      MTLRenderPassDescriptor *rpd = (__bridge MTLRenderPassDescriptor *)render_pass_descriptor_;
      [rpd release];
      render_pass_descriptor_ = nullptr;
    }
    if (command_queue_)
    {
      id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
      [queue release];
      command_queue_ = nullptr;
    }
    if (metal_device_)
    {
      id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
      [device release];
      metal_device_ = nullptr;
    }
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

void window_manager::initSDL()
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
  {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }
}

void window_manager::createWindow()
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

void window_manager::setupMetal()
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

void window_manager::initImGui()
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
  id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
  ImGui_ImplMetal_Init(device);
  ImGui_ImplSDL3_InitForMetal(window_);
}

void window_manager::shutdownImGui()
{
  if (initialized_)
  {
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
  }
}

int window_manager::run(RenderCallback renderCallback, UpdateCallback updateCallback)
{
  if (!initialized_)
  {
    std::cerr << "window_manager not initialized" << std::endl;
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

  // Timing for delta time calculation
  auto last_time = std::chrono::high_resolution_clock::now();

  // Main loop
  while (!should_close_)
  {
    @autoreleasepool
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
    }
  }

  // Remove event watch
  SDL_RemoveEventWatch(LiveResizeEventWatch, this);
  render_callback_ = nullptr;
  update_callback_ = nullptr;

  return 0;
}

bool window_manager::processEvents()
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

void window_manager::beginFrame()
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

void window_manager::endFrame()
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

std::pair<int, int> window_manager::getWindowSize() const
{
  if (!window_)
  {
    return {0, 0};
  }

  int width, height;
  SDL_GetWindowSize(window_, &width, &height);
  return {width, height};
}

bool window_manager::LiveResizeEventWatch(void* userdata, SDL_Event* event)
{
  window_manager* self = static_cast<window_manager*>(userdata);
  if (!self) return true;
  
  if (event->type == SDL_EVENT_WINDOW_EXPOSED)
  {
    // Check if this is a live-resize exposed event (data1 will be non-zero)
    if (event->window.data1 != 0)
    {
      // Mark that we're in live resize
      self->in_live_resize_.store(true);
      
      if (self->render_callback_)
      {
        // Render one frame during live-resize
        self->renderOneFrameLiveResize();
      }
    }
  }
  else if (event->type == SDL_EVENT_WINDOW_RESIZED)
  {
    // Clear live resize flag when resize completes
    self->in_live_resize_.store(false);
  }
  
  return true; // Return true to continue normal event processing
}

void window_manager::renderOneFrameLiveResize()
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

