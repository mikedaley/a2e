#include "video.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>

Video::Video(RAM &ram)
    : ram_(ram), flash_state_(false), frame_count_(0), frame_ready_(false)
{
  // Initialize soft switches to default state
  soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
  soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
  soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
  soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;

  // Initialize snapshot to match initial state
  soft_switches_snapshot_ = soft_switches_;

  // Initialize character ROM to a default pattern (filled circles) if not loaded
  char_rom_.fill(0x00);
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
  // Create surface for rendering at 80-column resolution (largest mode)
  // This allows switching between 40 and 80 column modes without recreating the surface
  int width = DISPLAY_WIDTH_80 * PIXEL_SCALE;
  int height = HIRES_HEIGHT * PIXEL_SCALE;

  surface_ = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  if (!surface_)
  {
    return false;
  }

  return true;
}

void Video::snapshotSoftSwitchState()
{
  // Snapshot all soft switch state at the start of frame rendering
  // This ensures consistent memory reads throughout the entire frame,
  // preventing random characters when soft switches change mid-frame
  soft_switches_snapshot_ = soft_switches_;
}

void Video::clearDisplayBuffer()
{
  if (!surface_)
  {
    return;
  }

  // Clear the entire display buffer to prevent stale data from previous frames
  SDL_LockSurface(surface_);
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);
  std::fill(pixels, pixels + (surface_->h * pitch), 0xFF000000); // Clear to black
  SDL_UnlockSurface(surface_);
}

void Video::setPixel(int x, int y, uint32_t color)
{
  if (!surface_)
  {
    return;
  }

  // Bounds check
  if (x < 0 || x >= surface_->w || y < 0 || y >= surface_->h)
  {
    return;
  }

  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);
  pixels[y * pitch + x] = color;
}

uint8_t Video::readVideoMemory(uint16_t addr) const
{
  // For video memory, we need to check if we should read from auxiliary memory
  // This method is used for:
  // - 40-column text mode
  // - 80-column text mode (uses both main and aux banks)
  // - Low-res and Hi-res graphics modes

  // Determine which bank to use based on snapshot state
  // For standard video modes, use the read_bank from the snapshot
  bool useAux = (soft_switches_snapshot_.read_bank == Apple2e::MemoryBank::AUX);

  // Use RAM's readDirect method for consistent access
  return ram_.readDirect(addr, useAux);
}

void Video::render()
{
  if (!surface_)
  {
    return;
  }

  frame_ready_ = false;

  // Update flash state for flashing characters
  frame_count_++;
  if (frame_count_ >= FRAMES_PER_FLASH)
  {
    frame_count_ = 0;
    flash_state_ = !flash_state_;
  }

  // Snapshot soft switch state at the start of frame rendering
  // This is critical: soft switches could change mid-frame if the CPU is running,
  // which would cause inconsistent memory reads and random characters
  snapshotSoftSwitchState();

  // Clear the entire display buffer before rendering to prevent stale data
  clearDisplayBuffer();

  // Lock surface for pixel access
  SDL_LockSurface(surface_);

  // Render based on current mode (using snapshot state)
  if (soft_switches_snapshot_.video_mode == Apple2e::VideoMode::TEXT)
  {
    renderTextMode();
  }
  else if (soft_switches_snapshot_.graphics_mode == Apple2e::GraphicsMode::LORES)
  {
    renderLoResMode();
  }
  else
  {
    renderHiResMode();
  }

  SDL_UnlockSurface(surface_);

  // Frame is now complete and ready for display
  frame_ready_ = true;
}

void Video::renderTextMode()
{
  if (!surface_)
  {
    return;
  }

  // Dispatch to appropriate text mode based on 80-column flag
  if (soft_switches_snapshot_.col80_mode)
  {
    renderTextMode80();
  }
  else
  {
    renderTextMode40();
  }
}

void Video::renderTextMode40()
{
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Use snapshot state for consistent frame rendering
  uint16_t text_base = (soft_switches_snapshot_.page_select == Apple2e::PageSelect::PAGE1)
                           ? Apple2e::MEM_TEXT_PAGE1_START
                           : Apple2e::MEM_TEXT_PAGE2_START;

  // Apple IIe character dimensions
  constexpr int CHAR_WIDTH = 7;
  constexpr int CHAR_HEIGHT = 8;

  // Colors for Apple IIe text mode (green phosphor monitor)
  constexpr uint32_t WHITE = 0xFF00FF00;  // Green phosphor
  constexpr uint32_t BLACK = 0xFF000000;

  // Apple IIe text screen row offsets (non-linear memory layout)
  static constexpr uint16_t kRowOffsets[24] = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

  // Render 40x24 text screen
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH_40; col++)
    {
      uint16_t addr = text_base + kRowOffsets[row] + col;

      // Read character from video memory using helper method
      // This properly handles aux bank selection based on snapshot state
      uint8_t ch = readVideoMemory(addr);

      // Apple IIe character encoding (matching text_renderer.hpp approach)
      bool isInverse = (ch & 0xC0) == 0;
      bool isFlash = (ch & 0xC0) == 0x40;

      // Character index: mask to 6 bits for inverse/flash, 7 bits otherwise
      uint8_t charIndex = ch & ((isFlash || isInverse) ? 0x3F : 0x7F);

      if (char_rom_loaded_)
      {
        // Get character data from ROM
        const uint8_t *charData = &char_rom_[charIndex * 8];

        // Draw character at screen position
        int screenX = col * CHAR_WIDTH;
        int screenY = row * CHAR_HEIGHT;

        for (int y = 0; y < CHAR_HEIGHT; y++)
        {
          uint8_t pattern = charData[y];

          // Invert pattern for inverse or flashing characters (exact approach from text_renderer)
          if (isInverse || (isFlash && flash_state_))
          {
            pattern = ~pattern;
          }

          int pixelY = screenY + y;

          for (int x = 0; x < CHAR_WIDTH; x++)
          {
            // Check bit x (bit 0 is leftmost) - exact approach from text_renderer
            bool pixelOn = (pattern & (1 << x)) != 0;
            uint32_t color = pixelOn ? WHITE : BLACK;

            // Draw scaled pixel
            int pixelX = screenX + x;
            for (int sy = 0; sy < PIXEL_SCALE; sy++)
            {
              for (int sx = 0; sx < PIXEL_SCALE; sx++)
              {
                int px = (pixelX * PIXEL_SCALE) + sx;
                int py = (pixelY * PIXEL_SCALE) + sy;
                if (px < surface_->w && py < surface_->h)
                {
                  pixels[py * pitch + px] = color;
                }
              }
            }
          }
        }
      }
      else
      {
        // Fallback: render colored blocks for debugging (if ROM not loaded)
        uint32_t fallback_color = 0xFFFFFFFF; // White

        int screenX = col * CHAR_WIDTH * PIXEL_SCALE;
        int screenY = row * CHAR_HEIGHT * PIXEL_SCALE;

        // Draw solid block
        for (int sy = 0; sy < CHAR_HEIGHT * PIXEL_SCALE; sy++)
        {
          for (int sx = 0; sx < CHAR_WIDTH * PIXEL_SCALE; sx++)
          {
            int px = screenX + sx;
            int py = screenY + sy;
            if (px < surface_->w && py < surface_->h)
            {
              pixels[py * pitch + px] = fallback_color;
            }
          }
        }
      }
    }
  }
}

void Video::renderTextMode80()
{
  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // 80-column mode only uses page 1 (cannot switch pages in 80-column mode)
  uint16_t text_base = Apple2e::MEM_TEXT_PAGE1_START;

  // Apple IIe character dimensions
  constexpr int CHAR_WIDTH = 7;
  constexpr int CHAR_HEIGHT = 8;

  // Colors for Apple IIe text mode (green phosphor monitor)
  constexpr uint32_t WHITE = 0xFF00FF00;  // Green phosphor
  constexpr uint32_t BLACK = 0xFF000000;

  // Apple IIe text screen row offsets (non-linear memory layout)
  static constexpr uint16_t kRowOffsets[24] = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

  // Render 80x24 text screen
  // Memory interleaving: even columns (0,2,4...) from AUX, odd columns (1,3,5...) from MAIN
  // Each bank stores 40 characters per row at the same addresses
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH_80; col++)
    {
      // Calculate memory address - each bank holds 40 chars, so divide col by 2
      uint16_t addr = text_base + kRowOffsets[row] + (col / 2);

      // Determine which bank to read from:
      // Even columns (0, 2, 4, ...) come from AUX memory
      // Odd columns (1, 3, 5, ...) come from MAIN memory
      bool useAux = (col % 2) == 0;
      
      // Read character directly from the appropriate bank
      uint8_t ch = ram_.readDirect(addr, useAux);

      // Apple IIe character encoding
      bool isInverse = (ch & 0xC0) == 0;
      bool isFlash = (ch & 0xC0) == 0x40;

      // Character index: mask to 6 bits for inverse/flash, 7 bits otherwise
      uint8_t charIndex = ch & ((isFlash || isInverse) ? 0x3F : 0x7F);

      if (char_rom_loaded_)
      {
        // Get character data from ROM
        const uint8_t *charData = &char_rom_[charIndex * 8];

        // Draw character at screen position
        int screenX = col * CHAR_WIDTH;
        int screenY = row * CHAR_HEIGHT;

        for (int y = 0; y < CHAR_HEIGHT; y++)
        {
          uint8_t pattern = charData[y];

          // Invert pattern for inverse or flashing characters
          if (isInverse || (isFlash && flash_state_))
          {
            pattern = ~pattern;
          }

          int pixelY = screenY + y;

          for (int x = 0; x < CHAR_WIDTH; x++)
          {
            // Check bit x (bit 0 is leftmost)
            bool pixelOn = (pattern & (1 << x)) != 0;
            uint32_t color = pixelOn ? WHITE : BLACK;

            // Draw scaled pixel
            int pixelX = screenX + x;
            for (int sy = 0; sy < PIXEL_SCALE; sy++)
            {
              for (int sx = 0; sx < PIXEL_SCALE; sx++)
              {
                int px = (pixelX * PIXEL_SCALE) + sx;
                int py = (pixelY * PIXEL_SCALE) + sy;
                if (px < surface_->w && py < surface_->h)
                {
                  pixels[py * pitch + px] = color;
                }
              }
            }
          }
        }
      }
      else
      {
        // Fallback: render colored blocks for debugging (if ROM not loaded)
        uint32_t fallback_color = 0xFFFFFFFF; // White

        int screenX = col * CHAR_WIDTH * PIXEL_SCALE;
        int screenY = row * CHAR_HEIGHT * PIXEL_SCALE;

        // Draw solid block
        for (int sy = 0; sy < CHAR_HEIGHT * PIXEL_SCALE; sy++)
        {
          for (int sx = 0; sx < CHAR_WIDTH * PIXEL_SCALE; sx++)
          {
            int px = screenX + sx;
            int py = screenY + sy;
            if (px < surface_->w && py < surface_->h)
            {
              pixels[py * pitch + px] = fallback_color;
            }
          }
        }
      }
    }
  }
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

      if (static_cast<size_t>(byte_index) < Apple2e::HIRES_PAGE_SIZE)
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
  (void)byte2; // Will be used when proper hi-res color artifacts are implemented
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
    // Return dimensions based on 40 or 80 column mode
    if (soft_switches_.col80_mode)
    {
      return {DISPLAY_WIDTH_80 * PIXEL_SCALE, TEXT_HEIGHT * 8 * PIXEL_SCALE};
    }
    else
    {
      return {DISPLAY_WIDTH_40 * PIXEL_SCALE, TEXT_HEIGHT * 8 * PIXEL_SCALE};
    }
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

bool Video::loadCharacterROM(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open())
  {
    // Don't print error - caller will try alternate paths
    return false;
  }

  // Read file size
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size < CHAR_ROM_SIZE)
  {
    std::cerr << "Warning: Character ROM file is too small (" << file_size
              << " bytes, expected at least " << CHAR_ROM_SIZE << " bytes)" << std::endl;
    return false;
  }

  // Apple IIe character ROM (341-0160-A) is 8KB containing 4 copies of the 2KB character set
  // We'll read the first 2KB which contains the primary character set
  file.read(reinterpret_cast<char *>(char_rom_.data()), CHAR_ROM_SIZE);

  if (!file)
  {
    std::cerr << "Warning: Failed to read character ROM data" << std::endl;
    return false;
  }

  char_rom_loaded_ = true;
  std::cout << "Successfully loaded character ROM from '" << filepath << "' (" << file_size << " bytes)" << std::endl;

  // Debug: Print first few characters to verify ROM is valid
  std::cout << "Character ROM check - char '@' (0x00) first byte: 0x"
            << std::hex << std::uppercase << static_cast<int>(char_rom_[0]) << std::dec << std::endl;

  return true;
}

