#include "ui/video_window.hpp"
#include "emulator/video_display.hpp"
#include <imgui.h>
#include <SDL3/SDL.h>

video_window::video_window()
{
}

video_window::~video_window()
{
}

void video_window::setKeyPressCallback(std::function<void(uint8_t)> callback)
{
  key_press_callback_ = std::move(callback);
}

uint8_t video_window::convertKeyCode(int key, bool shift, bool ctrl, bool caps_lock)
{
  // Convert ImGui key codes to Apple IIe key codes
  // Apple IIe uses 7-bit ASCII with high bit clear

  // Control key handling - Ctrl+A through Ctrl+Z produce codes 1-26
  if (ctrl && key >= ImGuiKey_A && key <= ImGuiKey_Z)
  {
    return static_cast<uint8_t>(1 + (key - ImGuiKey_A));
  }

  // Letter keys
  // CapsLock and Shift both affect case, but Shift inverts CapsLock state
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
  {
    bool uppercase = caps_lock != shift; // XOR: either one but not both
    if (uppercase)
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
    return 0x0D; // Carriage return
  case ImGuiKey_Escape:
    return 0x1B; // Escape
  case ImGuiKey_Backspace:
    return 0x08; // Backspace (left arrow on Apple II)
  case ImGuiKey_Delete:
    return 0x7F; // Delete
  case ImGuiKey_Tab:
    return 0x09; // Tab
  case ImGuiKey_LeftArrow:
    return 0x08; // Left arrow
  case ImGuiKey_RightArrow:
    return 0x15; // Right arrow (Ctrl+U)
  case ImGuiKey_UpArrow:
    return 0x0B; // Up arrow (Ctrl+K)
  case ImGuiKey_DownArrow:
    return 0x0A; // Down arrow (Ctrl+J)

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
    return 0xFF; // Not mappable
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

  // Check CapsLock state via SDL
  SDL_Keymod sdl_mods = SDL_GetModState();
  bool caps_lock = (sdl_mods & SDL_KMOD_CAPS) != 0;

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
    if (ImGui::IsKeyPressed((ImGuiKey)key, false)) // false = don't use ImGui's repeat
    {
      uint8_t apple_key = convertKeyCode(key, shift, ctrl, caps_lock);
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
  if (!open_ || !video_display_)
  {
    return;
  }

  // Update video display (generates new frame)
  video_display_->update();

  // Set initial window size - use 40-column dimensions for consistent window size
  int display_width_40 = video_display::getDisplayWidth40();
  int display_height = video_display::getDisplayHeight();
  ImGui::SetNextWindowSize(ImVec2(display_width_40 * 2.0f, display_height * 2.0f), ImGuiCond_FirstUseEver);

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

    void *texture = video_display_->getTexture();
    if (texture)
    {
      // Get available content region size
      ImVec2 content_size = ImGui::GetContentRegionAvail();

      // Use fixed aspect ratio based on 40-column mode (280x192)
      float content_aspect = static_cast<float>(display_width_40) / static_cast<float>(display_height);
      float window_aspect = content_size.x / content_size.y;

      // Calculate scaled size maintaining fixed aspect ratio
      float scaled_width, scaled_height;
      if (window_aspect > content_aspect)
      {
        // Window is wider than content - fit to height
        scaled_height = content_size.y;
        scaled_width = scaled_height * content_aspect;
      }
      else
      {
        // Window is taller than content - fit to width
        scaled_width = content_size.x;
        scaled_height = scaled_width / content_aspect;
      }

      // Calculate offset to center the image
      float offset_x = (content_size.x - scaled_width) * 0.5f;
      float offset_y = (content_size.y - scaled_height) * 0.5f;

      // Set cursor position to center the image
      ImVec2 cursor_pos = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursor_pos.x + offset_x, cursor_pos.y + offset_y));

      // Calculate UV coordinates to only show the portion of texture being used
      int current_width = video_display_->getCurrentDisplayWidth();
      int max_width = video_display::getMaxDisplayWidth();
      float uv_max_x = static_cast<float>(current_width) / static_cast<float>(max_width);
      ImVec2 uv_min(0.0f, 0.0f);
      ImVec2 uv_max(uv_max_x, 1.0f);

      // Display the texture centered with correct UV mapping
      ImGui::Image((ImTextureID)texture, ImVec2(scaled_width, scaled_height), uv_min, uv_max);
    }
  }
  ImGui::End();

  ImGui::PopStyleVar();
}
