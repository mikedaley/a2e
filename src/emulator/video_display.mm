#include "emulator/video_display.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

#import <Metal/Metal.h>

video_display::video_display()
{
  // Initialize frame buffer
  frame_buffer_.resize(DISPLAY_WIDTH * DISPLAY_HEIGHT, COLOR_BLACK);

  // Initialize character ROM to empty
  char_rom_.fill(0x00);
}

video_display::~video_display()
{
  if (texture_)
  {
    // Release the texture (manual memory management without ARC)
    id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_;
    [tex release];
    texture_ = nullptr;
  }
}

void video_display::setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  memory_read_callback_ = std::move(callback);
}

void video_display::setAuxMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  aux_memory_read_callback_ = std::move(callback);
}

void video_display::setVideoModeCallback(std::function<Apple2e::SoftSwitchState()> callback)
{
  video_mode_callback_ = std::move(callback);
}

bool video_display::loadCharacterROM(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open())
  {
    std::cerr << "Failed to open character ROM: " << filepath << std::endl;
    return false;
  }

  // Read file size
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size < CHAR_SET_SIZE)
  {
    std::cerr << "Character ROM file too small: " << file_size << " bytes" << std::endl;
    return false;
  }

  // Apple IIe character ROM layout (8KB total):
  // $0000-$07FF: Primary character set (normal characters)
  // $0800-$0FFF: Primary set repeated
  // $1000-$17FF: Alternate character set (MouseText at $40-$5F)
  // $1800-$1FFF: Alternate set repeated
  //
  // We load primary from offset $0000 and alternate from offset $1000

  // Read primary character set (first 2KB)
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(char_rom_.data()), CHAR_SET_SIZE);

  if (!file)
  {
    std::cerr << "Failed to read primary character ROM data" << std::endl;
    return false;
  }

  // Read alternate character set from offset $1000 if file is large enough
  if (file_size >= 0x1800) // Need at least $1000 + 2KB
  {
    file.seekg(0x1000, std::ios::beg);
    file.read(reinterpret_cast<char *>(char_rom_.data() + CHAR_ROM_ALT_OFFSET), CHAR_SET_SIZE);

    if (!file)
    {
      std::cerr << "Failed to read alternate character ROM data" << std::endl;
      return false;
    }
    std::cout << "Loaded character ROM with alternate set: " << filepath << std::endl;
  }
  else
  {
    // File doesn't have alternate set - copy primary set as fallback
    std::memcpy(char_rom_.data() + CHAR_ROM_ALT_OFFSET, char_rom_.data(), CHAR_SET_SIZE);
    std::cout << "Loaded character ROM (no alternate set): " << filepath << std::endl;
  }

  char_rom_loaded_ = true;
  return true;
}

bool video_display::initializeTexture(void *device)
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
  textureDescriptor.width = DISPLAY_WIDTH;
  textureDescriptor.height = DISPLAY_HEIGHT;
  textureDescriptor.usage = MTLTextureUsageShaderRead;
  // Use Shared storage mode to avoid synchronization issues between CPU writes and GPU reads
  textureDescriptor.storageMode = MTLStorageModeShared;

  // Create texture
  id<MTLTexture> tex = [mtlDevice newTextureWithDescriptor:textureDescriptor];
  if (!tex)
  {
    std::cerr << "Failed to create Metal texture" << std::endl;
    return false;
  }

  // Retain the texture (newTextureWithDescriptor returns +1, we keep it)
  texture_ = (__bridge void *)tex;
  texture_initialized_ = true;

  std::cout << "Video texture initialized: " << DISPLAY_WIDTH << "x" << DISPLAY_HEIGHT << std::endl;
  return true;
}

void video_display::update()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current video mode from soft switches
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  // Update current display width based on video mode
  bool is_80col = video_state.video_mode == Apple2e::VideoMode::TEXT &&
                  video_state.col80_mode;

  // Store effective 80-column mode for use by render functions
  effective_col80_mode_ = is_80col;

  if (is_80col)
  {
    current_display_width_ = DISPLAY_WIDTH_80;
  }
  else
  {
    current_display_width_ = DISPLAY_WIDTH_40;
  }

  // Detect mode change between 40 and 80 column - force full redraw
  if (is_80col != prev_col80_mode_)
  {
    prev_col80_mode_ = is_80col;
    // Clear the entire frame buffer to prevent stale content
    std::fill(frame_buffer_.begin(), frame_buffer_.end(), COLOR_BLACK);
  }

  // Update flash state (for text mode flashing characters)
  flash_counter_++;
  if (flash_counter_ >= FLASH_RATE)
  {
    flash_counter_ = 0;
    flash_state_ = !flash_state_;
  }

  // Clear frame buffer
  std::fill(frame_buffer_.begin(), frame_buffer_.end(), COLOR_BLACK);

  // Render based on video mode
  if (video_state.video_mode == Apple2e::VideoMode::TEXT)
  {
    // Pure text mode (40x24)
    renderTextMode();
  }
  else if (video_state.graphics_mode == Apple2e::GraphicsMode::HIRES)
  {
    // Hi-res graphics mode
    if (video_state.screen_mode == Apple2e::ScreenMode::MIXED)
    {
      // Mixed mode: hi-res top 160 lines, text bottom 4 lines
      renderHiResMode();
      // Render bottom 4 text lines (rows 20-23)
      uint16_t text_base = video_state.store80 ? TEXT_PAGE1_BASE
                                               : (video_state.page_select == Apple2e::PageSelect::PAGE1
                                                      ? TEXT_PAGE1_BASE
                                                      : TEXT_PAGE2_BASE);
      for (int row = 20; row < TEXT_HEIGHT; row++)
      {
        for (int col = 0; col < TEXT_WIDTH; col++)
        {
          uint16_t addr = text_base + ROW_OFFSETS[row] + col;
          uint8_t ch = memory_read_callback_(addr);
          drawCharacter(col, row, ch);
        }
      }
    }
    else
    {
      // Full screen hi-res
      renderHiResMode();
    }
  }
  else
  {
    // Lo-res graphics mode
    if (video_state.screen_mode == Apple2e::ScreenMode::MIXED)
    {
      // Mixed mode: lo-res top, text bottom 4 lines
      renderLoResMode();
      // Render bottom 4 text lines
      uint16_t text_base = video_state.store80 ? TEXT_PAGE1_BASE
                                               : (video_state.page_select == Apple2e::PageSelect::PAGE1
                                                      ? TEXT_PAGE1_BASE
                                                      : TEXT_PAGE2_BASE);
      for (int row = 20; row < TEXT_HEIGHT; row++)
      {
        for (int col = 0; col < TEXT_WIDTH; col++)
        {
          uint16_t addr = text_base + ROW_OFFSETS[row] + col;
          uint8_t ch = memory_read_callback_(addr);
          drawCharacter(col, row, ch);
        }
      }
    }
    else
    {
      // Full screen lo-res
      renderLoResMode();
    }
  }

  // Upload to texture
  if (texture_initialized_)
  {
    uploadTexture();
  }
}

void video_display::renderTextMode()
{
  if (!char_rom_loaded_)
  {
    return;
  }

  if (effective_col80_mode_)
  {
    renderTextMode80();
  }
  else
  {
    renderTextMode40();
  }
}

void video_display::renderTextMode40()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current video state
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  // When 80STORE is active, PAGE2 controls bank switching, not page selection.
  uint16_t text_base;
  if (video_state.store80)
  {
    text_base = TEXT_PAGE1_BASE; // Always page 1 when 80STORE is on
  }
  else
  {
    text_base = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                    ? TEXT_PAGE1_BASE
                    : TEXT_PAGE2_BASE;
  }

  // Render each character directly from memory (40-column mode)
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH_40; col++)
    {
      uint16_t addr = text_base + ROW_OFFSETS[row] + col;
      uint8_t ch = memory_read_callback_(addr);
      drawCharacter(col, row, ch);
    }
  }
}

void video_display::renderTextMode80()
{
  if (!memory_read_callback_ || !aux_memory_read_callback_)
  {
    return;
  }

  // 80-column mode only uses page 1
  uint16_t text_base = TEXT_PAGE1_BASE;

  // Render 80x24 text screen
  // Memory interleaving: even columns (0,2,4...) from AUX, odd columns (1,3,5...) from MAIN
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH_80; col++)
    {
      // Calculate memory address - each bank holds 40 chars, so divide col by 2
      uint16_t addr = text_base + ROW_OFFSETS[row] + (col / 2);

      // Determine which bank to read from:
      // Even columns (0, 2, 4, ...) come from AUX memory
      // Odd columns (1, 3, 5, ...) come from MAIN memory
      uint8_t ch;
      if ((col % 2) == 0)
      {
        ch = aux_memory_read_callback_(addr);
      }
      else
      {
        ch = memory_read_callback_(addr);
      }

      // Draw the character at the 80-column position
      drawCharacter80(col, row, ch);
    }
  }
}

void video_display::renderHiResMode()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current video state
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  // When 80STORE is on, PAGE2 controls bank switching, not page selection.
  uint16_t base_addr;
  if (video_state.store80)
  {
    base_addr = HIRES_PAGE1_BASE; // Always page 1 when 80STORE is on
  }
  else
  {
    base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                    ? HIRES_PAGE1_BASE
                    : HIRES_PAGE2_BASE;
  }

  // Determine number of lines to render (160 for mixed mode, 192 for full)
  int max_line = (video_state.screen_mode == Apple2e::ScreenMode::MIXED) ? 160 : 192;

  // Render each scan line
  for (int line = 0; line < max_line; line++)
  {
    // Calculate memory address for this line using Apple II interleaved layout
    uint16_t line_offset = ((line % 8) * 0x400) +
                           ((line / 64) * 0x28) +
                           (((line / 8) % 8) * 0x80);
    uint16_t line_addr = base_addr + line_offset;

    // Each line is 40 bytes, each byte represents 7 pixels
    for (int col = 0; col < 40; col++)
    {
      uint8_t byte = memory_read_callback_(line_addr + col);
      bool high_bit = (byte & 0x80) != 0; // Palette select bit

      // Get previous and next bytes for color bleeding at boundaries
      uint8_t prev_byte = (col > 0) ? memory_read_callback_(line_addr + col - 1) : 0;
      uint8_t next_byte = (col < 39) ? memory_read_callback_(line_addr + col + 1) : 0;

      // Extract the 7 pixels from this byte
      for (int bit = 0; bit < 7; bit++)
      {
        int x = col * 7 + bit;
        bool pixel_on = (byte & (1 << bit)) != 0;

        // Get adjacent bits for color determination
        bool prev_bit, next_bit;
        if (bit == 0)
        {
          prev_bit = (prev_byte & 0x40) != 0; // Bit 6 of previous byte
        }
        else
        {
          prev_bit = (byte & (1 << (bit - 1))) != 0;
        }

        if (bit == 6)
        {
          next_bit = (next_byte & 0x01) != 0; // Bit 0 of next byte
        }
        else
        {
          next_bit = (byte & (1 << (bit + 1))) != 0;
        }

        uint32_t color = getHiResColor(pixel_on, x, high_bit, prev_bit, next_bit);
        setPixel(x, line, color);
      }
    }
  }
}

uint32_t video_display::getHiResColor(bool bit_on, int x_pos, bool high_bit, bool prev_bit, bool next_bit)
{
  // If pixel is off, it's black (no fringing for off pixels)
  if (!bit_on)
  {
    return COLOR_BLACK;
  }

  // Pixel is on - determine color
  bool is_odd_column = (x_pos % 2) == 1;

  // Select base artifact color based on column position and high bit
  uint32_t artifact_color;
  if (!high_bit)
  {
    // Palette 1: Purple (even columns) / Green (odd columns)
    artifact_color = is_odd_column ? COLOR_GREEN_HIRES : COLOR_PURPLE;
  }
  else
  {
    // Palette 2: Blue (even columns) / Orange (odd columns)
    artifact_color = is_odd_column ? COLOR_ORANGE : COLOR_BLUE;
  }

  if (color_fringing_enabled_)
  {
    // With fringing: adjacent pixels blend to white
    if (prev_bit || next_bit)
    {
      return COLOR_WHITE;
    }
    return artifact_color;
  }
  else
  {
    // Without fringing: always show the artifact color
    return artifact_color;
  }
}

void video_display::renderLoResMode()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current video state
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  // When 80STORE is on, PAGE2 controls bank switching, not page selection.
  uint16_t base_addr;
  if (video_state.store80)
  {
    base_addr = TEXT_PAGE1_BASE; // Always page 1 when 80STORE is on
  }
  else
  {
    base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                    ? TEXT_PAGE1_BASE
                    : TEXT_PAGE2_BASE;
  }

  // Lo-res uses same memory layout as text mode
  // Determine number of rows to render (40 for mixed mode, 48 for full)
  int max_row = (video_state.screen_mode == Apple2e::ScreenMode::MIXED) ? 20 : 24;

  for (int row = 0; row < max_row; row++)
  {
    uint16_t row_addr = base_addr + ROW_OFFSETS[row];

    for (int col = 0; col < 40; col++)
    {
      uint8_t byte = memory_read_callback_(row_addr + col);

      // Bottom nibble is top block, top nibble is bottom block
      uint8_t top_color_idx = byte & 0x0F;
      uint8_t bottom_color_idx = (byte >> 4) & 0x0F;

      uint32_t top_color = LORES_COLORS[top_color_idx];
      uint32_t bottom_color = LORES_COLORS[bottom_color_idx];

      // Each lo-res block is 7 pixels wide and 4 pixels tall
      int screen_x = col * 7;
      int screen_y = row * 8; // Each text row = 2 lo-res rows = 8 pixels

      // Draw top block (4 rows)
      for (int y = 0; y < 4; y++)
      {
        for (int x = 0; x < 7; x++)
        {
          setPixel(screen_x + x, screen_y + y, top_color);
        }
      }

      // Draw bottom block (4 rows)
      for (int y = 4; y < 8; y++)
      {
        for (int x = 0; x < 7; x++)
        {
          setPixel(screen_x + x, screen_y + y, bottom_color);
        }
      }
    }
  }
}

void video_display::drawCharacter(int col, int row, uint8_t ch)
{
  // Apple IIe character encoding:
  // $00-$3F: Inverse (white on black becomes black on green)
  // $40-$7F: Flashing
  // $80-$FF: Normal

  bool is_inverse = (ch & 0xC0) == 0x00;
  bool is_flash = (ch & 0xC0) == 0x40;

  // Get character index (mask off high bits for inverse/flash)
  uint8_t char_index;
  if (is_inverse || is_flash)
  {
    char_index = ch & 0x3F;
  }
  else
  {
    char_index = ch & 0x7F;
  }

  // Determine character ROM offset based on ALTCHARSET mode
  size_t rom_offset = 0;
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
    if (video_state.altchar_mode)
    {
      rom_offset = CHAR_ROM_ALT_OFFSET;
    }
  }

  // Get character data from ROM (8 bytes per character)
  const uint8_t *char_data = &char_rom_[rom_offset + char_index * 8];

  // Calculate screen position
  int screen_x = col * CHAR_WIDTH;
  int screen_y = row * CHAR_HEIGHT;

  // Determine if we should show inverse (for inverse chars or flashing chars in flash state)
  bool show_inverse = is_inverse || (is_flash && flash_state_);

  // Draw the character (8 rows of 7 pixels each)
  for (int y = 0; y < CHAR_HEIGHT; y++)
  {
    uint8_t row_data = char_data[y];

    // Invert the pattern if needed
    if (show_inverse)
    {
      row_data = ~row_data;
    }

    for (int x = 0; x < CHAR_WIDTH; x++)
    {
      // Apple II character ROM has bit 0 as leftmost pixel
      bool pixel_on = (row_data & (1 << x)) != 0;
      uint32_t color = pixel_on ? COLOR_GREEN : COLOR_BLACK;

      setPixel(screen_x + x, screen_y + y, color);
    }
  }
}

void video_display::drawCharacter80(int col, int row, uint8_t ch)
{
  // Apple IIe character encoding (same as 40-column)
  bool is_inverse = (ch & 0xC0) == 0x00;
  bool is_flash = (ch & 0xC0) == 0x40;

  // Get character index (mask off high bits for inverse/flash)
  uint8_t char_index;
  if (is_inverse || is_flash)
  {
    char_index = ch & 0x3F;
  }
  else
  {
    char_index = ch & 0x7F;
  }

  // Determine character ROM offset based on ALTCHARSET mode
  size_t rom_offset = 0;
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
    if (video_state.altchar_mode)
    {
      rom_offset = CHAR_ROM_ALT_OFFSET;
    }
  }

  // Get character data from ROM (8 bytes per character)
  const uint8_t *char_data = &char_rom_[rom_offset + char_index * 8];

  // Calculate screen position - 80 columns use the full width
  int screen_x = col * CHAR_WIDTH;
  int screen_y = row * CHAR_HEIGHT;

  // Determine if we should show inverse
  bool show_inverse = is_inverse || (is_flash && flash_state_);

  // Draw the character (8 rows of 7 pixels each)
  for (int y = 0; y < CHAR_HEIGHT; y++)
  {
    uint8_t row_data = char_data[y];

    // Invert the pattern if needed
    if (show_inverse)
    {
      row_data = ~row_data;
    }

    for (int x = 0; x < CHAR_WIDTH; x++)
    {
      // Apple II character ROM has bit 0 as leftmost pixel
      bool pixel_on = (row_data & (1 << x)) != 0;
      uint32_t color = pixel_on ? COLOR_GREEN : COLOR_BLACK;

      setPixel(screen_x + x, screen_y + y, color);
    }
  }
}

void video_display::setPixel(int x, int y, uint32_t color)
{
  if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT)
  {
    frame_buffer_[y * DISPLAY_WIDTH + x] = color;
  }
}

void video_display::uploadTexture()
{
  if (!texture_)
  {
    return;
  }

  id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_;

  // Define the region to update
  MTLRegion region = MTLRegionMake2D(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  // Upload pixel data
  [tex replaceRegion:region
         mipmapLevel:0
           withBytes:frame_buffer_.data()
         bytesPerRow:DISPLAY_WIDTH * sizeof(uint32_t)];
}
