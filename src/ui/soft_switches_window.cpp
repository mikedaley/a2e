#include "ui/soft_switches_window.hpp"
#include "emulator/emulator.hpp"
#include <imgui.h>
#include <cstdio>

soft_switches_window::soft_switches_window(emulator& emu)
{
  // Set up state callback to get soft switch snapshot
  state_callback_ = [&emu]() -> Apple2e::SoftSwitchState
  {
    return emu.getSoftSwitchSnapshot();
  };
}

void soft_switches_window::renderSectionHeader(const char *label)
{
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", label);
  ImGui::Separator();
}

void soft_switches_window::renderSwitch(const char *label, bool state,
                                         const char *on_text, const char *off_text)
{
  // Fixed width for label column
  ImGui::Text("%-12s", label);
  ImGui::SameLine(120);

  // Color-coded state indicator
  if (state)
  {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", on_text);
  }
  else
  {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", off_text);
  }
}

void soft_switches_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(280, 450), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Soft Switches", &open_))
  {
    if (!state_callback_)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No state callback set");
      ImGui::End();
      return;
    }

    // Get current state (read-only snapshot)
    Apple2e::SoftSwitchState state = state_callback_();

    // Video Mode Section
    renderSectionHeader("Video Mode");

    renderSwitch("TEXT/GR", state.video_mode == Apple2e::VideoMode::TEXT, "TEXT", "GRAPHICS");
    renderSwitch("MIXED", state.screen_mode == Apple2e::ScreenMode::MIXED, "MIXED", "FULL");
    renderSwitch("PAGE", state.page_select == Apple2e::PageSelect::PAGE2, "PAGE 2", "PAGE 1");
    renderSwitch("HIRES", state.graphics_mode == Apple2e::GraphicsMode::HIRES, "HIRES", "LORES");
    renderSwitch("80COL", state.col80_mode, "80 COL", "40 COL");
    renderSwitch("ALTCHAR", state.altchar_mode, "ALT", "PRIMARY");

    // Memory Management Section
    renderSectionHeader("Memory Management");

    renderSwitch("80STORE", state.store80);
    renderSwitch("RAMRD", state.ramrd, "AUX", "MAIN");
    renderSwitch("RAMWRT", state.ramwrt, "AUX", "MAIN");
    renderSwitch("ALTZP", state.altzp, "AUX", "MAIN");
    renderSwitch("INTCXROM", state.intcxrom, "INTERNAL", "SLOT");
    renderSwitch("SLOTC3ROM", state.slotc3rom, "SLOT", "INTERNAL");
    renderSwitch("INTC8ROM", state.intc8rom, "INTERNAL", "SLOT");

    // Language Card Section
    renderSectionHeader("Language Card");

    renderSwitch("LC BANK", state.lcbank2, "BANK 2", "BANK 1");
    renderSwitch("LC READ", state.lcread, "RAM", "ROM");
    renderSwitch("LC WRITE", state.lcwrite, "ENABLED", "DISABLED");
    renderSwitch("LC PREWR", state.lcprewrite, "PRIMED", "RESET");

    // Other Section
    renderSectionHeader("Other");

    renderSwitch("KBD STROBE", state.keyboard_strobe);

    // Zero Page Vectors Section
    renderSectionHeader("Zero Page Vectors");

    // CSW - Character output Switch Vector
    char csw_buf[32];
    snprintf(csw_buf, sizeof(csw_buf), "$%04X", state.csw);
    ImGui::Text("%-12s", "CSW ($36)");
    ImGui::SameLine(120);
    // Color based on whether it points to 80-col firmware ($C300-$C3FF)
    bool csw_is_80col = (state.csw >= 0xC300 && state.csw <= 0xC3FF);
    if (csw_is_80col)
    {
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s (80col)", csw_buf);
    }
    else
    {
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", csw_buf);
    }

    // KSW - Keyboard input Switch Vector
    char ksw_buf[32];
    snprintf(ksw_buf, sizeof(ksw_buf), "$%04X", state.ksw);
    ImGui::Text("%-12s", "KSW ($38)");
    ImGui::SameLine(120);
    // Color based on whether it points to 80-col firmware ($C300-$C3FF)
    bool ksw_is_80col = (state.ksw >= 0xC300 && state.ksw <= 0xC3FF);
    if (ksw_is_80col)
    {
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s (80col)", ksw_buf);
    }
    else
    {
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", ksw_buf);
    }

  }
  ImGui::End();
}
