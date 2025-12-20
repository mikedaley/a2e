#include "windows/memory_viewer_window.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>

memory_viewer_window::memory_viewer_window()
{
  // Default to open
  setOpen(true);
}

void memory_viewer_window::setMemoryReadCallback(std::function<uint8_t(uint16_t)> callback)
{
  memory_read_callback_ = callback;
}

void memory_viewer_window::setBaseAddress(uint16_t address)
{
  // Align to 16-byte boundary for cleaner display
  base_address_ = address & 0xFFF0;

  // Update the input field
  snprintf(address_input_, sizeof(address_input_), "%04X", base_address_);
}

void memory_viewer_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_))
  {
    renderControls();
    ImGui::Separator();
    renderHexDump();
  }
  ImGui::End();
}

void memory_viewer_window::renderControls()
{
  // Address input
  ImGui::Text("Address:");
  ImGui::SameLine();
  ImGui::PushItemWidth(80);
  if (ImGui::InputText("##address", address_input_, sizeof(address_input_),
      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue))
  {
    // Parse hex input
    unsigned int addr;
    if (sscanf(address_input_, "%X", &addr) == 1)
    {
      setBaseAddress(static_cast<uint16_t>(addr));
    }
  }
  ImGui::PopItemWidth();

  ImGui::SameLine();
  if (ImGui::Button("Go"))
  {
    unsigned int addr;
    if (sscanf(address_input_, "%X", &addr) == 1)
    {
      setBaseAddress(static_cast<uint16_t>(addr));
    }
  }

  // Quick navigation buttons
  ImGui::SameLine();
  ImGui::Spacing();
  ImGui::SameLine();
  if (ImGui::Button("Zero Page"))
  {
    setBaseAddress(0x0000);
  }
  ImGui::SameLine();
  if (ImGui::Button("Stack"))
  {
    setBaseAddress(0x0100);
  }
  ImGui::SameLine();
  if (ImGui::Button("Text Page 1"))
  {
    setBaseAddress(0x0400);
  }
  ImGui::SameLine();
  if (ImGui::Button("ROM"))
  {
    setBaseAddress(0xD000);
  }

  // Rows to display
  ImGui::Text("Rows:");
  ImGui::SameLine();
  ImGui::PushItemWidth(100);
  ImGui::SliderInt("##rows", &rows_to_display_, 8, 64);
  ImGui::PopItemWidth();

  // Navigation buttons
  ImGui::SameLine();
  ImGui::Spacing();
  ImGui::SameLine();
  if (ImGui::Button("Page Up"))
  {
    int16_t new_addr = static_cast<int16_t>(base_address_) - (rows_to_display_ * 16);
    if (new_addr < 0) new_addr = 0;
    setBaseAddress(static_cast<uint16_t>(new_addr));
  }
  ImGui::SameLine();
  if (ImGui::Button("Page Down"))
  {
    uint32_t new_addr = static_cast<uint32_t>(base_address_) + (rows_to_display_ * 16);
    if (new_addr > 0xFFF0) new_addr = 0xFFF0;
    setBaseAddress(static_cast<uint16_t>(new_addr));
  }
}

void memory_viewer_window::renderHexDump()
{
  if (!memory_read_callback_)
  {
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No memory callback set!");
    return;
  }

  // Use monospace font if available
  ImFont* mono_font = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
  if (mono_font)
  {
    ImGui::PushFont(mono_font);
  }

  // Create a child window with scrolling
  ImGui::BeginChild("MemoryDump", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

  // Header
  ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Address  ");
  ImGui::SameLine();
  for (int i = 0; i < 16; i++)
  {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%02X ", i);
    ImGui::SameLine();
  }
  ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), " ASCII");

  ImGui::Separator();

  // Memory dump
  for (int row = 0; row < rows_to_display_; row++)
  {
    uint16_t row_address = base_address_ + (row * 16);

    // Address column
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "$%04X  ", row_address);
    ImGui::SameLine();

    // Hex bytes
    uint8_t bytes[16];
    for (int col = 0; col < 16; col++)
    {
      uint16_t addr = row_address + col;
      bytes[col] = memory_read_callback_(addr);

      // Highlight if this is the highlighted address
      bool is_highlighted = (addr == highlight_address_);
      if (is_highlighted)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
      }

      ImGui::Text("%02X", bytes[col]);

      if (is_highlighted)
      {
        ImGui::PopStyleColor();
      }

      if (col < 15)
      {
        ImGui::SameLine();
      }
    }

    // ASCII column
    ImGui::SameLine();
    ImGui::Text(" ");
    ImGui::SameLine();
    for (int col = 0; col < 16; col++)
    {
      char c = (bytes[col] >= 32 && bytes[col] < 127) ? bytes[col] : '.';
      ImGui::Text("%c", c);
      if (col < 15)
      {
        ImGui::SameLine();
      }
    }
  }

  ImGui::EndChild();

  if (mono_font)
  {
    ImGui::PopFont();
  }
}
