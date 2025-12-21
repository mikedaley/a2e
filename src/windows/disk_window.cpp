#include "windows/disk_window.hpp"
#include <imgui.h>
#include <cstdio>

disk_window::disk_window()
{
}

const char* disk_window::getSoftSwitchName(uint16_t address)
{
  switch (address & 0x0F)
  {
    case 0x00: return "PHASE0_OFF";
    case 0x01: return "PHASE0_ON";
    case 0x02: return "PHASE1_OFF";
    case 0x03: return "PHASE1_ON";
    case 0x04: return "PHASE2_OFF";
    case 0x05: return "PHASE2_ON";
    case 0x06: return "PHASE3_OFF";
    case 0x07: return "PHASE3_ON";
    case 0x08: return "MOTOR_OFF";
    case 0x09: return "MOTOR_ON";
    case 0x0A: return "DRIVE1_SEL";
    case 0x0B: return "DRIVE2_SEL";
    case 0x0C: return "Q6L (shift)";
    case 0x0D: return "Q6H (load)";
    case 0x0E: return "Q7L (read)";
    case 0x0F: return "Q7H (write)";
    default: return "???";
  }
}

void disk_window::logNibbleRead(uint8_t nibble, int track, int position)
{
  NibbleEntry entry{nibble, track, position};
  nibble_history_.push_back(entry);
  while (nibble_history_.size() > MAX_NIBBLE_HISTORY)
  {
    nibble_history_.pop_front();
  }
}

void disk_window::logSoftSwitch(uint16_t address, bool is_write, uint8_t value)
{
  SwitchEntry entry;
  entry.address = address;
  entry.is_write = is_write;
  entry.value = value;
  entry.name = getSoftSwitchName(address);
  
  switch_history_.push_back(entry);
  while (switch_history_.size() > MAX_SWITCH_HISTORY)
  {
    switch_history_.pop_front();
  }
}

void disk_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(450, 550), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_, ImGuiWindowFlags_MenuBar))
  {
    // Menu bar
    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu("View"))
      {
        ImGui::MenuItem("Nibble History", nullptr, &show_nibble_history_);
        ImGui::MenuItem("Switch History", nullptr, &show_switch_history_);
        ImGui::MenuItem("Auto-scroll", nullptr, &auto_scroll_);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Actions"))
      {
        if (ImGui::MenuItem("Clear History"))
        {
          nibble_history_.clear();
          switch_history_.clear();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    if (!disk2_)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Disk controller not connected");
      ImGui::End();
      return;
    }

    renderControllerState();
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Tabs for each drive
    if (ImGui::BeginTabBar("DriveTabs"))
    {
      if (ImGui::BeginTabItem("Drive 1"))
      {
        renderDriveState(0);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Drive 2"))
      {
        renderDriveState(1);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    if (show_nibble_history_)
    {
      ImGui::Spacing();
      renderNibbleHistory();
    }

    if (show_switch_history_)
    {
      ImGui::Spacing();
      renderSwitchHistory();
    }
  }
  ImGui::End();
}

void disk_window::renderControllerState()
{
  if (ImGui::CollapsingHeader("Controller State", ImGuiTreeNodeFlags_DefaultOpen))
  {
    // Motor status
    bool motor_on = disk2_->isMotorOn();
    ImVec4 motorColor = motor_on ? ImVec4(0.0f, 1.0f, 0.3f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    ImGui::TextColored(motorColor, "Motor: %s", motor_on ? "ON" : "OFF");
    
    ImGui::SameLine(120);
    
    // Selected drive
    int selected = disk2_->getSelectedDrive();
    ImGui::Text("Drive: %d", selected + 1);

    ImGui::SameLine(200);

    // Data latch
    uint8_t latch = disk2_->getDataLatch();
    ImGui::Text("Latch: $%02X", latch);

    // Q6/Q7 mode
    bool q6 = disk2_->getQ6();
    bool q7 = disk2_->getQ7();
    
    const char* mode_str;
    ImVec4 mode_color;
    if (!q7 && !q6) {
      mode_str = "READ (shift)";
      mode_color = ImVec4(0.0f, 1.0f, 0.5f, 1.0f);
    } else if (!q7 && q6) {
      mode_str = "SENSE WP";
      mode_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
    } else if (q7 && !q6) {
      mode_str = "WRITE (shift)";
      mode_color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    } else {
      mode_str = "WRITE (load)";
      mode_color = ImVec4(1.0f, 0.3f, 0.0f, 1.0f);
    }
    
    ImGui::TextColored(mode_color, "Mode: %s", mode_str);
    ImGui::SameLine(200);
    ImGui::Text("Q6=%d Q7=%d", q6 ? 1 : 0, q7 ? 1 : 0);

    // Stepper phases (shown as bitmask)
    uint8_t phase_mask = disk2_->getPhaseMask();
    ImGui::Text("Phases: ");
    ImGui::SameLine();
    for (int i = 0; i < 4; ++i) {
      if (i > 0) ImGui::SameLine();
      bool phase_on = (phase_mask >> i) & 1;
      ImVec4 phaseColor = phase_on ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
      ImGui::TextColored(phaseColor, "[%d]", i);
    }
    ImGui::SameLine();
    ImGui::Text(" (mask: 0x%X)", phase_mask);
  }
}

void disk_window::renderDriveState(int drive)
{
  bool has_disk = disk2_->hasDisk(drive);
  int track = disk2_->getCurrentTrack(drive);
  int nibble_pos = disk2_->getNibblePosition(drive);

  if (!has_disk)
  {
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No disk inserted");
    return;
  }

  // Disk info
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Disk inserted");
  
  // Track position
  ImGui::Text("Track: %2d / 34", track);
  ImGui::SameLine(150);
  ImGui::Text("Nibble Pos: %4d / 6656", nibble_pos);
  
  // Track position bar
  float track_progress = track / 34.0f;
  ImGui::ProgressBar(track_progress, ImVec2(200, 0), "");
  ImGui::SameLine();
  
  // Nibble position bar
  float nibble_progress = nibble_pos / 6656.0f;
  ImGui::ProgressBar(nibble_progress, ImVec2(-1, 0), "");
  
  // Visual track indicator
  ImGui::Text("Track:");
  ImGui::SameLine();
  
  // Draw track indicator as colored markers
  for (int t = 0; t < 35; ++t)
  {
    ImGui::SameLine(0, 1);
    
    if (t == track)
    {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "|");
    }
    else if (t % 5 == 0)
    {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "+");
    }
    else
    {
      ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "-");
    }
  }
  
  // Track numbers
  ImGui::Text("      0    5   10   15   20   25   30  34");
}

void disk_window::renderNibbleHistory()
{
  if (ImGui::CollapsingHeader("Recent Nibbles", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (nibble_history_.empty())
    {
      ImGui::TextDisabled("No nibbles read yet");
      return;
    }

    // Show last N nibbles in a scrolling region
    ImGui::BeginChild("NibbleScroll", ImVec2(0, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Header
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "Trk  Pos   Nib  Marker");
    ImGui::Separator();
    
    for (const auto& entry : nibble_history_)
    {
      // Highlight special marker bytes
      const char* marker = "";
      ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      
      if (entry.nibble == 0xD5)
      {
        marker = "<PROLOG1>";
        color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
      }
      else if (entry.nibble == 0xAA)
      {
        marker = "<PROLOG2>";
        color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
      }
      else if (entry.nibble == 0x96)
      {
        marker = "<ADDR>";
        color = ImVec4(0.0f, 1.0f, 0.5f, 1.0f);
      }
      else if (entry.nibble == 0xAD)
      {
        marker = "<DATA>";
        color = ImVec4(0.5f, 1.0f, 0.0f, 1.0f);
      }
      else if (entry.nibble == 0xDE)
      {
        marker = "<EPILOG1>";
        color = ImVec4(0.0f, 0.8f, 1.0f, 1.0f);
      }
      else if (entry.nibble == 0xEB)
      {
        marker = "<EPILOG3>";
        color = ImVec4(0.0f, 0.6f, 1.0f, 1.0f);
      }
      
      ImGui::TextColored(color, "%2d  %4d   $%02X  %s", 
                         entry.track, entry.position, entry.nibble, marker);
    }
    
    if (auto_scroll_)
    {
      ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
  }
}

void disk_window::renderSwitchHistory()
{
  if (ImGui::CollapsingHeader("Soft Switch Access", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (switch_history_.empty())
    {
      ImGui::TextDisabled("No soft switch accesses yet");
      return;
    }

    ImGui::BeginChild("SwitchScroll", ImVec2(0, 100), true);
    
    for (const auto& entry : switch_history_)
    {
      ImVec4 color = entry.is_write ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f) : ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
      ImGui::TextColored(color, "$%04X %s %s", 
                         entry.address, 
                         entry.is_write ? "W" : "R",
                         entry.name.c_str());
    }
    
    if (auto_scroll_)
    {
      ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
  }
}
