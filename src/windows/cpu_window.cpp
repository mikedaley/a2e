#include "windows/cpu_window.hpp"
#include <imgui.h>

cpu_window::cpu_window()
{
  // Default construction
}

void cpu_window::render()
{
  if (!open_)
  {
    return;
  }

  // Set initial position only on first use to prevent jumping during resize
  ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_))
  {
    if (state_.initialized)
    {
      ImGui::Text("Program Counter: 0x%04X", state_.pc);
      ImGui::Text("Stack Pointer:   0x%02X", static_cast<int>(state_.sp));
      ImGui::Text("Status Register: 0x%02X", static_cast<int>(state_.p));
      ImGui::Text("Accumulator:     0x%02X", static_cast<int>(state_.a));
      ImGui::Text("X Register:      0x%02X", static_cast<int>(state_.x));
      ImGui::Text("Y Register:      0x%02X", static_cast<int>(state_.y));
    }
    else
    {
      ImGui::Text("CPU not initialized");
    }
  }
  ImGui::End();
}

void cpu_window::setCPUState(const cpu_state& state)
{
  state_ = state;
}

