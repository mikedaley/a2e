#include "ui/disk_window.hpp"
#include "emulator/emulator.hpp"
#include "emulator/disk2_controller.hpp"
#include "emulator/disk_image.hpp"
#include "emulator/disk_formats/woz_disk_image.hpp"
#include <imgui.h>
#include <filesystem>

disk_window::disk_window(emulator& emu)
{
  // Set up callbacks to query disk controller state
  motor_on_callback_ = [&emu]() -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->isMotorOn() : false;
  };

  disk_ready_callback_ = [&emu]() -> bool
  {
    auto* disk = emu.getDiskController();
    // Disk is "ready" if controller exists and has a disk in the selected drive
    if (!disk) return false;
    return disk->hasDisk(disk->getSelectedDrive());
  };

  selected_drive_callback_ = [&emu]() -> int
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getSelectedDrive() : 0;
  };

  phase_states_callback_ = [&emu]() -> uint8_t
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getPhaseStates() : 0;
  };

  current_track_callback_ = [&emu]() -> int
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getCurrentTrack() : 0;
  };

  half_track_callback_ = [&emu]() -> int
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getHalfTrack() : 0;
  };

  // Callbacks for disk operations
  has_disk_callback_ = [&emu](int drive) -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->hasDisk(drive) : false;
  };

  get_disk_image_callback_ = [&emu](int drive) -> const DiskImage*
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getDiskImage(drive) : nullptr;
  };

  insert_disk_callback_ = [&emu](int drive, const std::string& path) -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->insertDisk(drive, path) : false;
  };

  eject_disk_callback_ = [&emu](int drive) -> void
  {
    auto* disk = emu.getDiskController();
    if (disk) disk->ejectDisk(drive);
  };

  // Create file browser dialog
  file_browser_ = std::make_unique<FileBrowserDialog>(
      "Select Disk Image",
      std::vector<std::string>{".woz", ".WOZ"});

  file_browser_->setSelectCallback([this](const std::string& path)
  {
    if (insert_disk_callback_)
    {
      insert_disk_callback_(pending_drive_, path);
    }
  });
}

void disk_window::renderSectionHeader(const char *label)
{
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", label);
  ImGui::Separator();
}

void disk_window::renderLED(const char *label, bool state,
                             uint32_t on_color, uint32_t off_color)
{
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();

  // LED circle parameters
  float radius = 6.0f;
  ImVec2 center(pos.x + radius + 2.0f, pos.y + radius + 2.0f);

  // Draw LED circle
  uint32_t color = state ? on_color : off_color;
  draw_list->AddCircleFilled(center, radius, color);

  // Draw border
  draw_list->AddCircle(center, radius, 0xFF666666, 0, 1.5f);

  // If LED is on, add a subtle glow effect
  if (state)
  {
    draw_list->AddCircle(center, radius + 2.0f, (color & 0x00FFFFFF) | 0x40000000, 0, 2.0f);
  }

  // Move cursor past the LED
  ImGui::Dummy(ImVec2(radius * 2 + 8.0f, radius * 2 + 4.0f));
  ImGui::SameLine();

  // Draw label
  ImGui::Text("%s", label);
}

std::string disk_window::getFilename(const std::string &path)
{
  std::filesystem::path p(path);
  return p.filename().string();
}

void disk_window::renderDrivePanel(int drive)
{
  bool has_disk = has_disk_callback_ ? has_disk_callback_(drive) : false;
  const DiskImage* image = get_disk_image_callback_ ? get_disk_image_callback_(drive) : nullptr;
  int selected_drive = selected_drive_callback_ ? selected_drive_callback_() : 0;
  bool is_selected = (drive == selected_drive);

  // Try to cast to WozDiskImage for additional info
  const WozDiskImage* woz_image = dynamic_cast<const WozDiskImage*>(image);

  // Drive panel with border
  ImGui::PushID(drive);

  // Highlight selected drive
  if (is_selected)
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.25f, 0.15f, 1.0f));
  }

  // Calculate panel height based on content
  float panel_height = has_disk ? 145.0f : 70.0f;

  if (ImGui::BeginChild("DrivePanel", ImVec2(0, panel_height), ImGuiChildFlags_Borders))
  {
    // Drive header with selection indicator
    if (is_selected)
    {
      ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Drive %d [SELECTED]", drive + 1);
    }
    else
    {
      ImGui::Text("Drive %d", drive + 1);
    }

    if (has_disk && image)
    {
      // Filename
      ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s",
                         getFilename(image->getFilepath()).c_str());

      // Format and disk type on same line
      if (woz_image)
      {
        ImGui::Text("%s %s", image->getFormatName().c_str(),
                    woz_image->getDiskTypeString().c_str());
      }
      else
      {
        ImGui::Text("Format: %s", image->getFormatName().c_str());
      }

      // Status flags on same line
      if (image->isWriteProtected())
      {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "[WP]");
      }

      // WOZ-specific info
      if (woz_image)
      {
        // Boot sector format
        std::string boot_fmt = woz_image->getBootSectorFormatString();
        if (boot_fmt != "Unknown")
        {
          ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", boot_fmt.c_str());
        }

        // Creator (if available)
        std::string creator = woz_image->getCreator();
        if (!creator.empty())
        {
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.7f, 1.0f), "Created by: %s", creator.c_str());
        }
      }

      // Eject button
      if (ImGui::Button("Eject"))
      {
        if (eject_disk_callback_)
        {
          eject_disk_callback_(drive);
        }
      }
    }
    else
    {
      // No disk - show load button
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(empty)");
      ImGui::Spacing();
      if (ImGui::Button("Load Disk..."))
      {
        pending_drive_ = drive;
        file_browser_->open();
      }
    }
  }
  ImGui::EndChild();

  if (is_selected)
  {
    ImGui::PopStyleColor();
  }

  ImGui::PopID();
}

void disk_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Disk II Controller", &open_))
  {
    // Drive Panels Section
    renderSectionHeader("Drives");

    renderDrivePanel(0);
    ImGui::Spacing();
    renderDrivePanel(1);

    // Status Section
    renderSectionHeader("Controller Status");

    // Motor LED (red when on)
    bool motor_on = motor_on_callback_ ? motor_on_callback_() : false;
    renderLED("Motor", motor_on, 0xFF0000FF, 0xFF333333);  // Red when on

    // Disk Ready LED (green when ready)
    bool disk_ready = disk_ready_callback_ ? disk_ready_callback_() : false;
    renderLED("Disk Ready", disk_ready, 0xFF00FF00, 0xFF333333);  // Green when ready

    // Track Position
    int half_track = half_track_callback_ ? half_track_callback_() : 0;
    int track = half_track / 2;
    bool is_half = (half_track % 2) != 0;

    if (is_half)
    {
      ImGui::Text("Track: %d.5", track);
    }
    else
    {
      ImGui::Text("Track: %d", track);
    }

    // Stepper Motor Phases Section
    renderSectionHeader("Stepper Phases");

    uint8_t phases = phase_states_callback_ ? phase_states_callback_() : 0;

    // Render phase LEDs in a horizontal row
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 8.0f;
    float spacing = 35.0f;

    for (int i = 0; i < 4; i++)
    {
      bool phase_on = (phases >> i) & 0x01;
      ImVec2 center(pos.x + radius + 4.0f + (i * spacing), pos.y + radius + 2.0f);

      // Draw LED - yellow/orange when on
      uint32_t color = phase_on ? 0xFF00CFFF : 0xFF333333;  // Orange when on
      draw_list->AddCircleFilled(center, radius, color);
      draw_list->AddCircle(center, radius, 0xFF666666, 0, 1.5f);

      // Glow effect when on
      if (phase_on)
      {
        draw_list->AddCircle(center, radius + 2.0f, 0x4000CFFF, 0, 2.0f);
      }
    }

    // Move cursor past the LEDs
    ImGui::Dummy(ImVec2(4 * spacing, radius * 2 + 8.0f));

    // Phase labels
    ImGui::Text("  0    1    2    3");
  }
  ImGui::End();

  // Render file browser dialog (must be outside main window)
  if (file_browser_)
  {
    file_browser_->render();
  }
}
