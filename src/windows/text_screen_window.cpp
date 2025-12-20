#include "windows/text_screen_window.hpp"
#include <imgui.h>

text_screen_window::text_screen_window()
    : memory_read_callback_(nullptr), text_page_(1), flash_state_(false), flash_counter_(0)
{
}

void text_screen_window::setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  memory_read_callback_ = std::move(callback);
}

void text_screen_window::setTextPage(int page)
{
  if (page == 1 || page == 2)
  {
    text_page_ = page;
  }
}

bool text_screen_window::loadCharacterROM(const uint8_t *data, size_t size)
{
  // Currently not using bitmap rendering, just ASCII approximation
  (void)data;
  (void)size;
  return true;
}

void text_screen_window::render()
{
  if (!open_)
  {
    return;
  }

  // Update flash state
  flash_counter_++;
  if (flash_counter_ >= FLASH_RATE)
  {
    flash_counter_ = 0;
    flash_state_ = !flash_state_;
  }

  ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Text Screen", &open_))
  {
    // Page selector
    ImGui::Text("Text Page:");
    ImGui::SameLine();
    if (ImGui::RadioButton("1 ($0400)", text_page_ == 1))
    {
      text_page_ = 1;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("2 ($0800)", text_page_ == 2))
    {
      text_page_ = 2;
    }

    ImGui::Separator();

    // Render the text display
    renderTextDisplay();
  }
  ImGui::End();
}

void text_screen_window::renderTextDisplay()
{
  if (!memory_read_callback_)
  {
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Memory read callback not set");
    return;
  }

  // Get base address for current page
  uint16_t base_addr = (text_page_ == 1) ? TEXT_PAGE1_BASE : TEXT_PAGE2_BASE;

  // Use a monospace font and green-on-black colors like an Apple II monitor
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));           // Green text
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));        // Black background

  // Create a child window for the text display with a border
  ImVec2 content_size = ImVec2(TEXT_WIDTH * 8.0f + 16.0f, TEXT_HEIGHT * 14.0f + 8.0f);
  
  if (ImGui::BeginChild("TextDisplay", content_size, ImGuiChildFlags_Borders))
  {
    // Build and render entire lines at once for better performance
    for (int row = 0; row < TEXT_HEIGHT; row++)
    {
      char line[TEXT_WIDTH + 1];

      // Read and convert entire row at once
      for (int col = 0; col < TEXT_WIDTH; col++)
      {
        uint16_t addr = base_addr + ROW_OFFSETS[row] + col;
        uint8_t ch = memory_read_callback_(addr);
        
        // Handle flashing - show space when flash is off for flashing chars
        if (isFlashing(ch) && !flash_state_)
        {
          line[col] = ' ';
        }
        else
        {
          line[col] = convertCharacter(ch);
        }
      }
      line[TEXT_WIDTH] = '\0';

      // Render entire line at once
      ImGui::TextUnformatted(line);
    }
  }
  ImGui::EndChild();

  ImGui::PopStyleColor(2);
}

char text_screen_window::convertCharacter(uint8_t code) const
{
  // Apple IIe character encoding:
  // $00-$1F: Inverse uppercase letters (@, A-Z, [, \, ], ^, _)
  // $20-$3F: Inverse special chars and numbers
  // $40-$5F: Flashing uppercase letters (@, A-Z, [, \, ], ^, _)
  // $60-$7F: Flashing special chars and numbers
  // $80-$9F: Normal uppercase letters (@, A-Z, [, \, ], ^, _)
  // $A0-$BF: Normal special chars and numbers
  // $C0-$DF: Normal uppercase letters (alternate)
  // $E0-$FF: Normal lowercase letters (on enhanced IIe)

  uint8_t charIndex;
  
  if ((code & 0xC0) == 0x00)
  {
    // Inverse: $00-$3F
    charIndex = code & 0x3F;
  }
  else if ((code & 0xC0) == 0x40)
  {
    // Flashing: $40-$7F
    charIndex = code & 0x3F;
  }
  else
  {
    // Normal: $80-$FF
    charIndex = code & 0x7F;
  }

  // Map character index to ASCII
  if (charIndex < 0x20)
  {
    // Control characters / special - map to uppercase letters or symbols
    return '@' + charIndex;
  }
  else if (charIndex < 0x40)
  {
    // Numbers and special characters ($20-$3F)
    return ' ' + (charIndex - 0x20);
  }
  else if (charIndex < 0x60)
  {
    // Uppercase letters and symbols ($40-$5F)
    return charIndex;
  }
  else
  {
    // Lowercase letters ($60-$7F)
    return charIndex;
  }
}

bool text_screen_window::isInverse(uint8_t code) const
{
  // Inverse video: characters $00-$3F
  return (code & 0xC0) == 0x00;
}

bool text_screen_window::isFlashing(uint8_t code) const
{
  // Flashing: characters $40-$7F
  return (code & 0xC0) == 0x40;
}
