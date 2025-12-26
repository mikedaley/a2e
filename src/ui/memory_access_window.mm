#include "ui/memory_access_window.hpp"
#include "emulator/emulator.hpp"
#include <imgui.h>

#import <Metal/Metal.h>

memory_access_window::memory_access_window(emulator &emu)
{
  // Initialize frame buffer with grey
  frame_buffer_.resize(TEXTURE_SIZE * TEXTURE_SIZE, COLOR_GREY);

  // Set up callbacks
  get_tracker_ = [&emu]()
  {
    return emu.getAccessTracker();
  };

  peek_memory_ = [&emu](uint16_t addr)
  {
    return emu.peekMemory(addr);
  };
}

memory_access_window::~memory_access_window()
{
  if (texture_)
  {
    // Release the Metal texture
    id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_;
    [tex release];
    texture_ = nullptr;
  }
}

bool memory_access_window::initializeTexture(void *device)
{
  if (!device)
  {
    return false;
  }

  device_ = device;
  id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device;

  // Create texture descriptor
  MTLTextureDescriptor *textureDescriptor = [[MTLTextureDescriptor alloc] init];
  textureDescriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
  textureDescriptor.width = TEXTURE_SIZE;
  textureDescriptor.height = TEXTURE_SIZE;
  textureDescriptor.usage = MTLTextureUsageShaderRead;
  textureDescriptor.storageMode = MTLStorageModeShared;

  // Create texture
  id<MTLTexture> tex = [mtlDevice newTextureWithDescriptor:textureDescriptor];
  if (!tex)
  {
    return false;
  }

  // Store without ARC - we own the reference
  texture_ = (__bridge void *)tex;
  texture_initialized_ = true;

  return true;
}

void memory_access_window::update([[maybe_unused]] float deltaTime)
{
  if (!open_ || !texture_initialized_)
  {
    return;
  }

  auto *tracker = get_tracker_();
  if (!tracker)
  {
    return;
  }

  // Enable tracking when window is open
  tracker->setEnabled(true);

  // Update tracker fade timers
  tracker->update(deltaTime);

  // Update frame buffer and upload texture
  updateFrameBuffer();
  uploadTexture();
}

void memory_access_window::updateFrameBuffer()
{
  auto *tracker = get_tracker_();
  if (!tracker)
  {
    return;
  }

  for (uint32_t addr = 0; addr < 65536; ++addr)
  {
    const auto &entry = tracker->getEntry(static_cast<uint16_t>(addr));

    if (entry.type == access_type::NONE || entry.fade_timer <= 0.0f)
    {
      frame_buffer_[addr] = COLOR_GREY;
    }
    else
    {
      // Calculate fade factor (1.0 = full color, 0.0 = grey)
      float fade = entry.fade_timer / memory_access_tracker::FADE_DURATION;

      uint32_t target_color = (entry.type == access_type::READ) ? COLOR_GREEN : COLOR_RED;
      frame_buffer_[addr] = interpolateColor(COLOR_GREY, target_color, fade);
    }
  }
}

uint32_t memory_access_window::interpolateColor(uint32_t colorA, uint32_t colorB, float t) const
{
  // Extract components (ABGR format)
  uint8_t a_a = (colorA >> 24) & 0xFF;
  uint8_t b_a = (colorA >> 16) & 0xFF;
  uint8_t g_a = (colorA >> 8) & 0xFF;
  uint8_t r_a = colorA & 0xFF;

  uint8_t a_b = (colorB >> 24) & 0xFF;
  uint8_t b_b = (colorB >> 16) & 0xFF;
  uint8_t g_b = (colorB >> 8) & 0xFF;
  uint8_t r_b = colorB & 0xFF;

  // Interpolate
  uint8_t a = static_cast<uint8_t>(a_a + (a_b - a_a) * t);
  uint8_t b = static_cast<uint8_t>(b_a + (b_b - b_a) * t);
  uint8_t g = static_cast<uint8_t>(g_a + (g_b - g_a) * t);
  uint8_t r = static_cast<uint8_t>(r_a + (r_b - r_a) * t);

  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}

void memory_access_window::uploadTexture()
{
  if (!texture_)
  {
    return;
  }

  id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_;
  MTLRegion region = MTLRegionMake2D(0, 0, TEXTURE_SIZE, TEXTURE_SIZE);
  [tex replaceRegion:region
         mipmapLevel:0
           withBytes:frame_buffer_.data()
         bytesPerRow:TEXTURE_SIZE * sizeof(uint32_t)];
}

void memory_access_window::render()
{
  if (!open_)
  {
    // Disable tracking when window is closed
    auto *tracker = get_tracker_();
    if (tracker)
    {
      tracker->setEnabled(false);
    }
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(550, 600), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(getName(), &open_))
  {
    ImGui::End();
    return;
  }

  // Zoom controls
  ImGui::Text("Zoom: %.1fx", zoom_level_);
  ImGui::SameLine();
  if (ImGui::Button("-") && zoom_level_ > MIN_ZOOM)
  {
    zoom_level_ -= 0.5f;
  }
  ImGui::SameLine();
  if (ImGui::Button("+") && zoom_level_ < MAX_ZOOM)
  {
    zoom_level_ += 0.5f;
  }
  ImGui::SameLine();
  ImGui::Text("  ");
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0, 1, 0, 1), "Read");
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(1, 0, 0, 1), "Write");
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "None");

  ImGui::Separator();

  // Scrollable image region
  ImVec2 canvas_size = ImVec2(TEXTURE_SIZE * zoom_level_,
                               TEXTURE_SIZE * zoom_level_);

  ImGui::BeginChild("MemoryCanvas", ImVec2(0, 0), true,
                    ImGuiWindowFlags_HorizontalScrollbar);

  if (texture_initialized_ && texture_)
  {
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

    ImGui::Image((ImTextureID)texture_, canvas_size,
                 ImVec2(0, 0), ImVec2(1, 1));

    // Handle mouse hover for tooltip
    if (ImGui::IsItemHovered())
    {
      ImVec2 mouse_pos = ImGui::GetMousePos();

      // Calculate which pixel is under the mouse
      float rel_x = (mouse_pos.x - cursor_pos.x) / zoom_level_;
      float rel_y = (mouse_pos.y - cursor_pos.y) / zoom_level_;

      if (rel_x >= 0 && rel_x < TEXTURE_SIZE &&
          rel_y >= 0 && rel_y < TEXTURE_SIZE)
      {
        int pixel_x = static_cast<int>(rel_x);
        int pixel_y = static_cast<int>(rel_y);
        uint16_t address = static_cast<uint16_t>(pixel_y * 256 + pixel_x);

        renderTooltip(address);
      }

      // Handle mouse wheel zoom
      float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0)
      {
        zoom_level_ += wheel * 0.5f;
        if (zoom_level_ < MIN_ZOOM)
          zoom_level_ = MIN_ZOOM;
        if (zoom_level_ > MAX_ZOOM)
          zoom_level_ = MAX_ZOOM;
      }
    }
  }
  else
  {
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Texture not initialized");
  }

  ImGui::EndChild();
  ImGui::End();
}

void memory_access_window::renderTooltip(uint16_t address)
{
  ImGui::BeginTooltip();

  uint8_t value = peek_memory_(address);

  ImGui::Text("Address: $%04X", address);
  ImGui::Text("Value:   $%02X (%d)", value, value);
  ImGui::Text("Region:  %s", getRegionName(address));

  // Show access state
  auto *tracker = get_tracker_();
  if (tracker)
  {
    const auto &entry = tracker->getEntry(address);
    const char *access = "None";
    if (entry.fade_timer > 0)
    {
      if (entry.type == access_type::READ)
      {
        access = "Read (recent)";
      }
      else if (entry.type == access_type::WRITE)
      {
        access = "Write (recent)";
      }
    }
    ImGui::Text("Access:  %s", access);
  }

  ImGui::EndTooltip();
}

const char *memory_access_window::getRegionName(uint16_t address) const
{
  if (address < 0x0100)
  {
    return "Zero Page";
  }
  else if (address < 0x0200)
  {
    return "Stack";
  }
  else if (address >= 0x0400 && address <= 0x07FF)
  {
    return "Text Page 1";
  }
  else if (address >= 0x0800 && address <= 0x0BFF)
  {
    return "Text Page 2";
  }
  else if (address >= 0x2000 && address <= 0x3FFF)
  {
    return "HiRes Page 1";
  }
  else if (address >= 0x4000 && address <= 0x5FFF)
  {
    return "HiRes Page 2";
  }
  else if (address >= 0xC000 && address <= 0xC0FF)
  {
    return "I/O (Soft Switches)";
  }
  else if (address >= 0xC100 && address <= 0xCFFF)
  {
    return "Expansion ROM";
  }
  else if (address >= 0xD000)
  {
    return "ROM / Language Card";
  }
  else if (address < 0xC000)
  {
    return "RAM";
  }

  return "Unknown";
}
