#include "windows/status_window.hpp"
#include <imgui.h>

status_window::status_window(ImGuiIO* io_ptr)
    : io_ptr_(io_ptr)
{
}

void status_window::render()
{
  if (!open_)
  {
    return;
  }

  // Set initial position only on first use to prevent jumping during resize
  ImGui::SetNextWindowPos(ImVec2(340, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_))
  {
    ImGui::Text("Apple 2e Emulator v0.1.0");
    ImGui::Text("65C02 CPU initialized");
    ImGui::Separator();

    if (io_ptr_)
    {
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                  1000.0f / io_ptr_->Framerate,
                  io_ptr_->Framerate);
    }
  }
  ImGui::End();
}

