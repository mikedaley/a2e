#include "video.hpp"
#include <algorithm>
#include <cstring>

Video::Video(RAM &ram)
    : ram_(ram)
{
  // Initialize soft switches to default state
  soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
  soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
  soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
  soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
}

Video::~Video()
{
  if (surface_)
  {
    SDL_DestroySurface(surface_);
  }
}

uint8_t Video::read(uint16_t address)
{
  // Video memory is accessed through RAM, so we just pass through
  // Soft switches are handled by MMU
  if (address >= Apple2e::MEM_TEXT_PAGE1_START && address <= Apple2e::MEM_TEXT_PAGE2_END)
  {
    // Text page access - handled by RAM
    return 0x00; // Video doesn't directly read, MMU routes to RAM
  }
  else if (address >= Apple2e::MEM_HIRES_PAGE1_START && address <= Apple2e::MEM_HIRES_PAGE2_END)
  {
    // Hi-res page access - handled by RAM
    return 0x00; // Video doesn't directly read, MMU routes to RAM
  }

  return 0x00;
}

void Video::write(uint16_t address, uint8_t value)
{
  // Video memory writes go through RAM, we just need to mark for redraw
  // The actual rendering happens in render()
  (void)address;
  (void)value;
}

AddressRange Video::getAddressRange() const
{
  // Video handles soft switches and video memory regions
  // Note: Actual memory is in RAM, but video needs to know about these regions
  return {Apple2e::MEM_TEXT_PAGE1_START, Apple2e::MEM_HIRES_PAGE2_END};
}

std::string Video::getName() const
{
  return "Video";
}

bool Video::initialize()
{
  // Create surface for rendering (Apple IIe native resolution)
  int width = HIRES_WIDTH * PIXEL_SCALE;
  int height = HIRES_HEIGHT * PIXEL_SCALE;

  surface_ = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  if (!surface_)
  {
    return false;
  }

  return true;
}

void Video::render()
{
  if (!surface_ || !texture_)
  {
    return;
  }

  // Lock surface for pixel access
  SDL_LockSurface(surface_);
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Clear to black
  std::fill(pixels, pixels + (surface_->h * pitch), 0x00000000);

  // Render based on current mode
  if (soft_switches_.video_mode == Apple2e::VideoMode::TEXT)
  {
    renderTextMode();
  }
  else if (soft_switches_.graphics_mode == Apple2e::GraphicsMode::LORES)
  {
    renderLoResMode();
  }
  else
  {
    renderHiResMode();
  }

  SDL_UnlockSurface(surface_);
}

void Video::renderTextMode()
{
  if (!surface_)
  {
    return;
  }

  SDL_LockSurface(surface_);
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Determine which text page to use
  uint16_t text_base = (soft_switches_.page_select == Apple2e::PageSelect::PAGE1)
                           ? Apple2e::MEM_TEXT_PAGE1_START
                           : Apple2e::MEM_TEXT_PAGE2_START;

  // Get appropriate RAM bank
  const auto &ram_bank = (soft_switches_.read_bank == Apple2e::MemoryBank::MAIN)
                             ? ram_.getMainBank()
                             : ram_.getAuxBank();

  // Render 40x24 text screen
  // Each character is 7x8 pixels, but we'll use a simpler 8x8 for now
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH; col++)
    {
      uint16_t addr = text_base + (row * TEXT_WIDTH) + col;
      if (addr >= Apple2e::MEM_TEXT_PAGE1_START && addr <= Apple2e::MEM_TEXT_PAGE2_END)
      {
        uint8_t char_code = ram_bank[addr - Apple2e::MEM_RAM_START];
        // Simple character rendering (ASCII, inverse video handled by bit 7)
        bool inverse = (char_code & 0x80) != 0;
        uint8_t char_val = char_code & 0x7F;

        // For now, render as simple colored blocks
        // TODO: Implement proper character ROM rendering
        uint32_t color = inverse ? 0xFFFFFFFF : 0x00000000;
        uint32_t bg_color = inverse ? 0x00000000 : 0xFFFFFFFF;

        // Draw 8x8 character cell
        int x = col * 8 * PIXEL_SCALE;
        int y = row * 8 * PIXEL_SCALE;
        for (int py = 0; py < 8 * PIXEL_SCALE; py++)
        {
          for (int px = 0; px < 8 * PIXEL_SCALE; px++)
          {
            int pixel_x = x + px;
            int pixel_y = y + py;
            if (pixel_x < surface_->w && pixel_y < surface_->h)
            {
              pixels[pixel_y * pitch + pixel_x] = bg_color;
            }
          }
        }
      }
    }
  }

  SDL_UnlockSurface(surface_);
}

void Video::renderLoResMode()
{
  if (!surface_)
  {
    return;
  }

  SDL_LockSurface(surface_);
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Lo-res graphics: 40x48 pixels, 2 bits per pixel (4 colors)
  // Each byte contains 4 pixels (2 bits each)
  // Memory starts at $0400

  // Get appropriate RAM bank
  const auto &ram_bank = (soft_switches_.read_bank == Apple2e::MemoryBank::MAIN)
                             ? ram_.getMainBank()
                             : ram_.getAuxBank();

  uint16_t base_addr = Apple2e::MEM_TEXT_PAGE1_START - Apple2e::MEM_RAM_START;

  for (int row = 0; row < LORES_HEIGHT; row++)
  {
    for (int col = 0; col < LORES_WIDTH; col++)
    {
      int byte_index = (row * 40) + col;
      uint8_t byte = ram_bank[base_addr + byte_index];

      // Each byte has 4 pixels (2 bits each)
      for (int pixel = 0; pixel < 4; pixel++)
      {
        int bit_pair = pixel;
        uint32_t color = getLoResColor(byte, bit_pair);

        int x = (col * 4 + pixel) * PIXEL_SCALE;
        int y = row * PIXEL_SCALE;
        for (int py = 0; py < PIXEL_SCALE; py++)
        {
          for (int px = 0; px < PIXEL_SCALE; px++)
          {
            int pixel_x = x + px;
            int pixel_y = y + py;
            if (pixel_x < surface_->w && pixel_y < surface_->h)
            {
              pixels[pixel_y * pitch + pixel_x] = color;
            }
          }
        }
      }
    }
  }

  SDL_UnlockSurface(surface_);
}

void Video::renderHiResMode()
{
  if (!surface_)
  {
    return;
  }

  SDL_LockSurface(surface_);
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Hi-res graphics: 280x192 pixels
  // Determine which page to use
  uint16_t hires_base = (soft_switches_.page_select == Apple2e::PageSelect::PAGE1)
                            ? Apple2e::MEM_HIRES_PAGE1_START
                            : Apple2e::MEM_HIRES_PAGE2_START;

  // Get appropriate RAM bank
  const auto &ram_bank = (soft_switches_.read_bank == Apple2e::MemoryBank::MAIN)
                             ? ram_.getMainBank()
                             : ram_.getAuxBank();

  uint16_t base_addr = hires_base - Apple2e::MEM_RAM_START;

  // Hi-res uses a complex bit pattern
  // Each byte represents 7 pixels (1 bit each, but with color artifacts)
  for (int row = 0; row < HIRES_HEIGHT; row++)
  {
    for (int col = 0; col < HIRES_WIDTH; col++)
    {
      int byte_col = col / 7;
      int bit_offset = col % 7;
      int byte_index = (row * 40) + byte_col;

      if (byte_index < Apple2e::HIRES_PAGE_SIZE)
      {
        uint8_t byte1 = ram_bank[base_addr + byte_index];
        uint8_t byte2 = (byte_col < 39) ? ram_bank[base_addr + byte_index + 1] : 0;

        uint32_t color = getHiResColor(byte1, byte2, bit_offset);

        int x = col * PIXEL_SCALE;
        int y = row * PIXEL_SCALE;
        for (int py = 0; py < PIXEL_SCALE; py++)
        {
          for (int px = 0; px < PIXEL_SCALE; px++)
          {
            int pixel_x = x + px;
            int pixel_y = y + py;
            if (pixel_x < surface_->w && pixel_y < surface_->h)
            {
              pixels[pixel_y * pitch + pixel_x] = color;
            }
          }
        }
      }
    }
  }

  SDL_UnlockSurface(surface_);
}

uint32_t Video::getLoResColor(uint8_t byte, int bit_pair) const
{
  // Extract 2-bit color value
  int shift = (3 - bit_pair) * 2;
  int color_index = (byte >> shift) & 0x03;
  return colorIndexToRGB(color_index);
}

uint32_t Video::getHiResColor(uint8_t byte1, uint8_t byte2, int bit_offset) const
{
  // Hi-res color is complex - depends on bit patterns and adjacent bits
  // Simplified version for now
  int bit = 6 - bit_offset;
  bool pixel_on = (byte1 & (1 << bit)) != 0;

  if (!pixel_on)
  {
    return 0x00000000; // Black
  }

  // Determine color based on bit position and adjacent bits
  // This is a simplified version - real Apple IIe has color artifacts
  int color_index = (bit_offset % 2) + ((byte1 >> 5) & 0x02);
  return colorIndexToRGB(color_index);
}

uint32_t Video::colorIndexToRGB(int color_index) const
{
  // Apple IIe color palette (simplified)
  static const uint32_t palette[16] = {
      0x000000, // 0: Black
      0x682C00, // 1: Dark Red
      0x287400, // 2: Dark Green
      0x7C7C00, // 3: Dark Yellow
      0x004088, // 4: Dark Blue
      0x7C3C7C, // 5: Dark Magenta
      0x007C7C, // 6: Dark Cyan
      0x909090, // 7: Light Gray
      0x404040, // 8: Dark Gray
      0xFF4400, // 9: Red
      0x44FF00, // 10: Green
      0xFFFF00, // 11: Yellow
      0x0088FF, // 12: Blue
      0xFF00FF, // 13: Magenta
      0x00FFFF, // 14: Cyan
      0xFFFFFF  // 15: White
  };

  if (color_index >= 0 && color_index < 16)
  {
    return palette[color_index] | 0xFF000000; // Add alpha
  }
  return 0x00000000;
}

void Video::updateSoftSwitches(const Apple2e::SoftSwitchState &state)
{
  soft_switches_ = state;
}

std::pair<int, int> Video::getDimensions() const
{
  if (soft_switches_.video_mode == Apple2e::VideoMode::TEXT)
  {
    return {TEXT_WIDTH * 8 * PIXEL_SCALE, TEXT_HEIGHT * 8 * PIXEL_SCALE};
  }
  else if (soft_switches_.graphics_mode == Apple2e::GraphicsMode::LORES)
  {
    return {LORES_WIDTH * 4 * PIXEL_SCALE, LORES_HEIGHT * PIXEL_SCALE};
  }
  else
  {
    return {HIRES_WIDTH * PIXEL_SCALE, HIRES_HEIGHT * PIXEL_SCALE};
  }
}

const uint32_t *Video::getPixels() const
{
  if (!surface_)
  {
    return nullptr;
  }
  return static_cast<const uint32_t *>(surface_->pixels);
}

size_t Video::getPixelCount() const
{
  if (!surface_)
  {
    return 0;
  }
  return static_cast<size_t>(surface_->w * surface_->h);
}

