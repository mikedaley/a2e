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

  uint32_t *pixels = static_cast<uint32_t *>(surface_->pixels);
  int pitch = surface_->pitch / sizeof(uint32_t);

  // Hi-res graphics: 280x192 pixels
  // Determine which page to use
  uint16_t hires_base = (soft_switches_snapshot_.page_select == Apple2e::PageSelect::PAGE1)
                            ? Apple2e::MEM_HIRES_PAGE1_START
                            : Apple2e::MEM_HIRES_PAGE2_START;

  // Apple II hi-res color palette (from apple2js)
  static constexpr uint32_t BLACK   = 0xFF000000;
  static constexpr uint32_t WHITE   = 0xFFFFFFFF;
  static constexpr uint32_t VIOLET  = 0xFFFF44FD;  // Violet (even column, high bit clear)
  static constexpr uint32_t GREEN   = 0xFF14F53C;  // Green (odd column, high bit clear)
  static constexpr uint32_t BLUE    = 0xFF14CFFD;  // Blue (even column, high bit set)
  static constexpr uint32_t ORANGE  = 0xFFFF6A3C;  // Orange (odd column, high bit set)

  // Apple IIe hi-res screen row base addresses (non-linear memory layout)
  // Each group of 8 rows shares a base, offset by $0400 for each row in the group
  static constexpr uint16_t kRowBase[192] = {
    0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00,
    0x0080, 0x0480, 0x0880, 0x0C80, 0x1080, 0x1480, 0x1880, 0x1C80,
    0x0100, 0x0500, 0x0900, 0x0D00, 0x1100, 0x1500, 0x1900, 0x1D00,
    0x0180, 0x0580, 0x0980, 0x0D80, 0x1180, 0x1580, 0x1980, 0x1D80,
    0x0200, 0x0600, 0x0A00, 0x0E00, 0x1200, 0x1600, 0x1A00, 0x1E00,
    0x0280, 0x0680, 0x0A80, 0x0E80, 0x1280, 0x1680, 0x1A80, 0x1E80,
    0x0300, 0x0700, 0x0B00, 0x0F00, 0x1300, 0x1700, 0x1B00, 0x1F00,
    0x0380, 0x0780, 0x0B80, 0x0F80, 0x1380, 0x1780, 0x1B80, 0x1F80,
    0x0028, 0x0428, 0x0828, 0x0C28, 0x1028, 0x1428, 0x1828, 0x1C28,
    0x00A8, 0x04A8, 0x08A8, 0x0CA8, 0x10A8, 0x14A8, 0x18A8, 0x1CA8,
    0x0128, 0x0528, 0x0928, 0x0D28, 0x1128, 0x1528, 0x1928, 0x1D28,
    0x01A8, 0x05A8, 0x09A8, 0x0DA8, 0x11A8, 0x15A8, 0x19A8, 0x1DA8,
    0x0228, 0x0628, 0x0A28, 0x0E28, 0x1228, 0x1628, 0x1A28, 0x1E28,
    0x02A8, 0x06A8, 0x0AA8, 0x0EA8, 0x12A8, 0x16A8, 0x1AA8, 0x1EA8,
    0x0328, 0x0728, 0x0B28, 0x0F28, 0x1328, 0x1728, 0x1B28, 0x1F28,
    0x03A8, 0x07A8, 0x0BA8, 0x0FA8, 0x13A8, 0x17A8, 0x1BA8, 0x1FA8,
    0x0050, 0x0450, 0x0850, 0x0C50, 0x1050, 0x1450, 0x1850, 0x1C50,
    0x00D0, 0x04D0, 0x08D0, 0x0CD0, 0x10D0, 0x14D0, 0x18D0, 0x1CD0,
    0x0150, 0x0550, 0x0950, 0x0D50, 0x1150, 0x1550, 0x1950, 0x1D50,
    0x01D0, 0x05D0, 0x09D0, 0x0DD0, 0x11D0, 0x15D0, 0x19D0, 0x1DD0,
    0x0250, 0x0650, 0x0A50, 0x0E50, 0x1250, 0x1650, 0x1A50, 0x1E50,
    0x02D0, 0x06D0, 0x0AD0, 0x0ED0, 0x12D0, 0x16D0, 0x1AD0, 0x1ED0,
    0x0350, 0x0750, 0x0B50, 0x0F50, 0x1350, 0x1750, 0x1B50, 0x1F50,
    0x03D0, 0x07D0, 0x0BD0, 0x0FD0, 0x13D0, 0x17D0, 0x1BD0, 0x1FD0,
  };

  for (int row = 0; row < HIRES_HEIGHT; row++)
  {
    uint16_t row_addr = hires_base + kRowBase[row];
    
    for (int byte_col = 0; byte_col < 40; byte_col++)
    {
      // Read current byte and neighbors for artifact color calculation
      uint8_t prev_byte = (byte_col > 0) ? readVideoMemory(row_addr + byte_col - 1) : 0;
      uint8_t curr_byte = readVideoMemory(row_addr + byte_col);
      uint8_t next_byte = (byte_col < 39) ? readVideoMemory(row_addr + byte_col + 1) : 0;
      
      // High bit determines color palette (bit 7)
      bool high_bit_set = (curr_byte & 0x80) != 0;
      
      // Determine colors based on high bit
      uint32_t odd_color = high_bit_set ? ORANGE : GREEN;
      uint32_t even_color = high_bit_set ? BLUE : VIOLET;
      
      // Process 7 pixels per byte (bits 0-6)
      for (int bit = 0; bit < 7; bit++)
      {
        int screen_col = byte_col * 7 + bit;
        bool pixel_on = (curr_byte & (1 << bit)) != 0;
        
        uint32_t color;
        
        if (!pixel_on)
        {
          color = BLACK;
        }
        else
        {
          // Determine if this is an odd or even column (for color selection)
          // Column 0, 2, 4, 6... are even; 1, 3, 5... are odd
          bool is_odd_column = (screen_col & 1) != 0;
          
          if (color_fringing_enabled_)
          {
            // Check adjacent pixels for white fringing
            bool prev_pixel_on = false;
            bool next_pixel_on = false;
            
            if (bit > 0)
            {
              prev_pixel_on = (curr_byte & (1 << (bit - 1))) != 0;
            }
            else if (byte_col > 0)
            {
              prev_pixel_on = (prev_byte & 0x40) != 0; // Bit 6 of previous byte
            }
            
            if (bit < 6)
            {
              next_pixel_on = (curr_byte & (1 << (bit + 1))) != 0;
            }
            else if (byte_col < 39)
            {
              next_pixel_on = (next_byte & 0x01) != 0; // Bit 0 of next byte
            }
            
            // Adjacent pixels create white
            if (prev_pixel_on || next_pixel_on)
            {
              color = WHITE;
            }
            else
            {
              color = is_odd_column ? odd_color : even_color;
            }
          }
          else
          {
            // No fringing - just show the artifact color directly
            color = is_odd_column ? odd_color : even_color;
          }
        }
        
        // Draw scaled pixel
        int x = screen_col * PIXEL_SCALE;
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

