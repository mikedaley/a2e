#include "windows/video_window.hpp"
#include <imgui.h>
#include <fstream>
#include <iostream>

#import <Metal/Metal.h>

video_window::video_window()
{
  // Initialize frame buffer
  frame_buffer_.resize(DISPLAY_WIDTH * DISPLAY_HEIGHT, COLOR_BLACK);

  // Initialize character ROM to empty
  char_rom_.fill(0x00);

  // Initialize text page cache
  prev_text_page_.fill(0x00);
}

video_window::~video_window()
{
  if (texture_)
  {
    // Release the texture (manual memory management without ARC)
    id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_;
    [tex release];
    texture_ = nullptr;
  }
}

void video_window::setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  memory_read_callback_ = std::move(callback);
}

void video_window::setKeyPressCallback(std::function<void(uint8_t)> callback)
{
  key_press_callback_ = std::move(callback);
}

void video_window::setVideoModeCallback(std::function<Apple2e::SoftSwitchState()> callback)
{
  video_mode_callback_ = std::move(callback);
}

bool video_window::loadCharacterROM(const std::string &filepath)
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

  if (file_size < CHAR_ROM_SIZE)
  {
    std::cerr << "Character ROM file too small: " << file_size << " bytes" << std::endl;
    return false;
  }

  // Apple IIe character ROM (341-0160-A) is 8KB with 4 copies of the character set
  // Read the first 2KB
  file.read(reinterpret_cast<char *>(char_rom_.data()), CHAR_ROM_SIZE);

  if (!file)
  {
    std::cerr << "Failed to read character ROM data" << std::endl;
    return false;
  }

  char_rom_loaded_ = true;
  std::cout << "Loaded character ROM: " << filepath << std::endl;
  return true;
}

bool video_window::initializeTexture(void *device)
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
  // Managed mode requires explicit synchronization which we don't have access to here
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

void video_window::updateDisplay()
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

  // Update flash state (for text mode flashing characters)
  flash_counter_++;
  if (flash_counter_ >= FLASH_RATE)
  {
    flash_counter_ = 0;
    flash_state_ = !flash_state_;
  }

  // Check if flash state changed (need redraw for flashing chars)
  if (flash_state_ != prev_flash_state_)
  {
    prev_flash_state_ = flash_state_;
    needs_redraw_ = true;
  }

  // For graphics modes, always redraw (memory could change anywhere)
  // For text mode, check for changes in text page
  if (video_state.video_mode == Apple2e::VideoMode::TEXT)
  {
    uint16_t base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                         ? TEXT_PAGE1_BASE : TEXT_PAGE2_BASE;
    bool text_changed = false;

    for (int row = 0; row < TEXT_HEIGHT; row++)
    {
      for (int col = 0; col < TEXT_WIDTH; col++)
      {
        int idx = row * TEXT_WIDTH + col;
        uint16_t addr = base_addr + ROW_OFFSETS[row] + col;
        uint8_t ch = memory_read_callback_(addr);

        if (ch != prev_text_page_[idx])
        {
          prev_text_page_[idx] = ch;
          text_changed = true;
        }
      }
    }

    if (text_changed)
    {
      needs_redraw_ = true;
    }
  }
  else
  {
    // Graphics mode - always redraw for now
    needs_redraw_ = true;
  }

  // Only redraw if something changed
  if (!needs_redraw_)
  {
    return;
  }

  needs_redraw_ = false;

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
      for (int row = 20; row < TEXT_HEIGHT; row++)
      {
        for (int col = 0; col < TEXT_WIDTH; col++)
        {
          uint16_t base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                               ? TEXT_PAGE1_BASE : TEXT_PAGE2_BASE;
          uint16_t addr = base_addr + ROW_OFFSETS[row] + col;
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
      for (int row = 20; row < TEXT_HEIGHT; row++)
      {
        for (int col = 0; col < TEXT_WIDTH; col++)
        {
          uint16_t base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                               ? TEXT_PAGE1_BASE : TEXT_PAGE2_BASE;
          uint16_t addr = base_addr + ROW_OFFSETS[row] + col;
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

void video_window::renderTextMode()
{
  if (!char_rom_loaded_)
  {
    return;
  }

  // Render each character from cached text page data
  for (int row = 0; row < TEXT_HEIGHT; row++)
  {
    for (int col = 0; col < TEXT_WIDTH; col++)
    {
      int idx = row * TEXT_WIDTH + col;
      uint8_t ch = prev_text_page_[idx];
      drawCharacter(col, row, ch);
    }
  }
}

void video_window::renderHiResMode()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current page
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  uint16_t base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                       ? HIRES_PAGE1_BASE : HIRES_PAGE2_BASE;

  // Determine number of lines to render (160 for mixed mode, 192 for full)
  int max_line = (video_state.screen_mode == Apple2e::ScreenMode::MIXED) ? 160 : 192;

  // Render each scan line
  for (int line = 0; line < max_line; line++)
  {
    // Calculate memory address for this line using Apple II interleaved layout
    // Formula: base + (line % 8) * 0x400 + (line / 64) * 0x28 + ((line / 8) % 8) * 0x80
    uint16_t line_offset = ((line % 8) * 0x400) +
                           ((line / 64) * 0x28) +
                           (((line / 8) % 8) * 0x80);
    uint16_t line_addr = base_addr + line_offset;

    // Each line is 40 bytes, each byte represents 7 pixels
    for (int col = 0; col < 40; col++)
    {
      uint8_t byte = memory_read_callback_(line_addr + col);
      bool high_bit = (byte & 0x80) != 0;  // Palette select bit

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
          prev_bit = (prev_byte & 0x40) != 0;  // Bit 6 of previous byte
        }
        else
        {
          prev_bit = (byte & (1 << (bit - 1))) != 0;
        }

        if (bit == 6)
        {
          next_bit = (next_byte & 0x01) != 0;  // Bit 0 of next byte
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

uint32_t video_window::getHiResColor(bool bit_on, int x_pos, bool high_bit, bool prev_bit, bool next_bit)
{
  // If pixel is off and both neighbors are off, it's black
  if (!bit_on && !prev_bit && !next_bit)
  {
    return COLOR_BLACK;
  }

  // If pixel is on and both neighbors are on, it's white
  if (bit_on && prev_bit && next_bit)
  {
    return COLOR_WHITE;
  }

  // If pixel is on with at least one neighbor on, it's white
  if (bit_on && (prev_bit || next_bit))
  {
    return COLOR_WHITE;
  }

  // Single pixel on - color depends on position and palette
  if (bit_on)
  {
    bool is_odd_column = (x_pos % 2) == 1;

    if (!high_bit)
    {
      // Palette 1: Purple (odd columns) / Green (even columns)
      return is_odd_column ? COLOR_PURPLE : COLOR_GREEN_HIRES;
    }
    else
    {
      // Palette 2: Blue (odd columns) / Orange (even columns)
      return is_odd_column ? COLOR_BLUE : COLOR_ORANGE;
    }
  }

  // Pixel is off but has a neighbor on - creates color fringing
  // This simulates NTSC artifact coloring
  if (prev_bit || next_bit)
  {
    bool is_odd_column = (x_pos % 2) == 1;

    if (!high_bit)
    {
      return is_odd_column ? COLOR_PURPLE : COLOR_GREEN_HIRES;
    }
    else
    {
      return is_odd_column ? COLOR_BLUE : COLOR_ORANGE;
    }
  }

  return COLOR_BLACK;
}

void video_window::renderLoResMode()
{
  if (!memory_read_callback_)
  {
    return;
  }

  // Get current page and mode
  Apple2e::SoftSwitchState video_state;
  if (video_mode_callback_)
  {
    video_state = video_mode_callback_();
  }

  uint16_t base_addr = (video_state.page_select == Apple2e::PageSelect::PAGE1)
                       ? TEXT_PAGE1_BASE : TEXT_PAGE2_BASE;

  // Lo-res uses same memory layout as text mode
  // Each byte represents 2 vertical blocks (top nibble, bottom nibble)
  // Screen is 40x48 blocks, each block is 7x4 pixels

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
      int screen_y = row * 8;  // Each text row = 2 lo-res rows = 8 pixels

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

void video_window::drawCharacter(int col, int row, uint8_t ch)
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

  // Get character data from ROM (8 bytes per character)
  const uint8_t *char_data = &char_rom_[char_index * 8];

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

void video_window::setPixel(int x, int y, uint32_t color)
{
  if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT)
  {
    frame_buffer_[y * DISPLAY_WIDTH + x] = color;
  }
}

void video_window::uploadTexture()
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

uint8_t video_window::convertKeyCode(int key, bool shift, bool ctrl)
{
  // Convert ImGui key codes to Apple IIe key codes
  // Apple IIe uses 7-bit ASCII with high bit clear

  // Control key handling - Ctrl+A through Ctrl+Z produce codes 1-26
  if (ctrl && key >= ImGuiKey_A && key <= ImGuiKey_Z)
  {
    return static_cast<uint8_t>(1 + (key - ImGuiKey_A));
  }

  // Letter keys
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
  {
    if (shift)
    {
      return static_cast<uint8_t>('A' + (key - ImGuiKey_A));
    }
    else
    {
      return static_cast<uint8_t>('a' + (key - ImGuiKey_A));
    }
  }

  // Number keys (top row)
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
  {
    if (shift)
    {
      // Shifted number keys produce symbols
      static const char shifted[] = ")!@#$%^&*(";
      return static_cast<uint8_t>(shifted[key - ImGuiKey_0]);
    }
    else
    {
      return static_cast<uint8_t>('0' + (key - ImGuiKey_0));
    }
  }

  // Special keys
  switch (key)
  {
    case ImGuiKey_Space:
      return ' ';
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter:
      return 0x0D;  // Carriage return
    case ImGuiKey_Escape:
      return 0x1B;  // Escape
    case ImGuiKey_Backspace:
      return 0x08;  // Backspace (left arrow on Apple II)
    case ImGuiKey_Delete:
      return 0x7F;  // Delete
    case ImGuiKey_Tab:
      return 0x09;  // Tab
    case ImGuiKey_LeftArrow:
      return 0x08;  // Left arrow
    case ImGuiKey_RightArrow:
      return 0x15;  // Right arrow (Ctrl+U)
    case ImGuiKey_UpArrow:
      return 0x0B;  // Up arrow (Ctrl+K)
    case ImGuiKey_DownArrow:
      return 0x0A;  // Down arrow (Ctrl+J)

    // Punctuation
    case ImGuiKey_Comma:
      return shift ? '<' : ',';
    case ImGuiKey_Period:
      return shift ? '>' : '.';
    case ImGuiKey_Slash:
      return shift ? '?' : '/';
    case ImGuiKey_Semicolon:
      return shift ? ':' : ';';
    case ImGuiKey_Apostrophe:
      return shift ? '"' : '\'';
    case ImGuiKey_LeftBracket:
      return shift ? '{' : '[';
    case ImGuiKey_RightBracket:
      return shift ? '}' : ']';
    case ImGuiKey_Backslash:
      return shift ? '|' : '\\';
    case ImGuiKey_Minus:
      return shift ? '_' : '-';
    case ImGuiKey_Equal:
      return shift ? '+' : '=';
    case ImGuiKey_GraveAccent:
      return shift ? '~' : '`';

    default:
      return 0xFF;  // Not mappable
  }
}

void video_window::handleKeyboardInput()
{
  if (!key_press_callback_)
  {
    return;
  }

  ImGuiIO &io = ImGui::GetIO();

  // Check modifier keys
  bool shift = io.KeyShift;
  bool ctrl = io.KeyCtrl;

  // Check if the currently held key was released
  if (held_key_ != -1 && !ImGui::IsKeyDown((ImGuiKey)held_key_))
  {
    held_key_ = -1;
    held_apple_key_ = 0;
    repeat_timer_ = 0.0f;
    repeat_started_ = false;
  }

  // Check for new key presses
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++)
  {
    if (ImGui::IsKeyPressed((ImGuiKey)key, false))  // false = don't use ImGui's repeat
    {
      uint8_t apple_key = convertKeyCode(key, shift, ctrl);
      if (apple_key != 0xFF)
      {
        // Send the initial keypress
        key_press_callback_(apple_key);

        // Start tracking for repeat
        held_key_ = key;
        held_apple_key_ = apple_key;
        repeat_timer_ = 0.0f;
        repeat_started_ = false;
      }
    }
  }

  // Handle key repeat for held key
  if (held_key_ != -1 && held_apple_key_ != 0)
  {
    repeat_timer_ += io.DeltaTime;

    if (!repeat_started_)
    {
      // Wait for initial delay before starting repeat
      if (repeat_timer_ >= REPEAT_DELAY)
      {
        repeat_started_ = true;
        repeat_timer_ = 0.0f;
        key_press_callback_(held_apple_key_);
      }
    }
    else
    {
      // Repeat at the specified rate
      if (repeat_timer_ >= REPEAT_RATE)
      {
        repeat_timer_ -= REPEAT_RATE;
        key_press_callback_(held_apple_key_);
      }
    }
  }
}

void video_window::render()
{
  if (!open_)
  {
    return;
  }

  // Update display from memory
  updateDisplay();

  // Set initial window size
  ImGui::SetNextWindowSize(ImVec2(DISPLAY_WIDTH * 2.0f, DISPLAY_HEIGHT * 2.0f), ImGuiCond_FirstUseEver);

  // Window flags: no scrollbars, no padding for clean display
  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  // Push zero padding for the window content
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  if (ImGui::Begin("Video Display", &open_, window_flags))
  {
    // Handle keyboard input when window is focused
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
      handleKeyboardInput();
    }

    if (texture_initialized_)
    {
      // Get available content region size
      ImVec2 content_size = ImGui::GetContentRegionAvail();

      // Calculate aspect ratios
      float texture_aspect = static_cast<float>(DISPLAY_WIDTH) / static_cast<float>(DISPLAY_HEIGHT);
      float window_aspect = content_size.x / content_size.y;

      // Calculate scaled size maintaining aspect ratio
      float scaled_width, scaled_height;
      if (window_aspect > texture_aspect)
      {
        // Window is wider than texture - fit to height
        scaled_height = content_size.y;
        scaled_width = scaled_height * texture_aspect;
      }
      else
      {
        // Window is taller than texture - fit to width
        scaled_width = content_size.x;
        scaled_height = scaled_width / texture_aspect;
      }

      // Calculate offset to center the image
      float offset_x = (content_size.x - scaled_width) * 0.5f;
      float offset_y = (content_size.y - scaled_height) * 0.5f;

      // Set cursor position to center the image
      ImVec2 cursor_pos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursor_pos.x + offset_x, cursor_pos.y + offset_y));

      // Display the texture centered
      ImGui::Image((ImTextureID)texture_, ImVec2(scaled_width, scaled_height));
    }
  }
  ImGui::End();

  ImGui::PopStyleVar();
}
