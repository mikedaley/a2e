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
    return disk ? disk->getCurrentTrack() : -1;
  };

  quarter_track_callback_ = [&emu]() -> int
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getQuarterTrack() : -1;
  };

  q6_callback_ = [&emu]() -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getQ6() : false;
  };

  q7_callback_ = [&emu]() -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getQ7() : false;
  };

  write_mode_callback_ = [&emu]() -> bool
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->isWriteMode() : false;
  };

  data_latch_callback_ = [&emu]() -> uint8_t
  {
    auto* disk = emu.getDiskController();
    return disk ? disk->getDataLatch() : 0;
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

  // Create disk callback - creates a new DOS 3.3 formatted disk and inserts it
  create_disk_callback_ = [&emu](int drive, const std::string& path) -> bool
  {
    auto* disk = emu.getDiskController();
    if (!disk) return false;

    // Create a new DOS 3.3 formatted disk image
    auto new_disk = WozDiskImage::createEmptyDOS33Disk(path);
    if (!new_disk)
    {
      return false;
    }

    // Insert the newly created disk
    return disk->insertDisk(drive, path);
  };

  // Create file browser dialog for loading disks
  file_browser_ = std::make_unique<FileBrowserDialog>(
      "Select Disk Image",
      std::vector<std::string>{".woz", ".WOZ", ".dsk", ".DSK", ".do", ".DO", ".po", ".PO"});

  file_browser_->setSelectCallback([this](const std::string& path)
  {
    if (insert_disk_callback_)
    {
      insert_disk_callback_(pending_drive_, path);
    }
  });

  // Create file browser dialog for saving new disks
  save_file_browser_ = std::make_unique<FileBrowserDialog>(
      "Create New Disk",
      std::vector<std::string>{".woz"},
      FileBrowserMode::Save);

  save_file_browser_->setDefaultFilename("NewDisk.woz");

  save_file_browser_->setSelectCallback([this](const std::string& path)
  {
    if (create_disk_callback_)
    {
      create_disk_callback_(pending_drive_, path);
    }
  });
}

void disk_window::renderSectionHeader(const char *label)
{
  ImGui::Separator();
  ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", label);
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
  bool motor_on = motor_on_callback_ ? motor_on_callback_() : false;

  // Try to cast to WozDiskImage for additional info
  const WozDiskImage* woz_image = dynamic_cast<const WozDiskImage*>(image);

  ImGui::PushID(drive);

  // Highlight selected drive with subtle border
  if (is_selected && motor_on)
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.18f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.8f, 0.3f, 0.8f));
  }
  else if (is_selected)
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.12f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.5f, 0.3f, 0.6f));
  }

  // Compact panel height
  float panel_height = has_disk ? 68.0f : 44.0f;

  if (ImGui::BeginChild("DrivePanel", ImVec2(0, panel_height), ImGuiChildFlags_Borders))
  {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Drive LED indicator
    float led_radius = 4.0f;
    ImVec2 led_center(pos.x + led_radius + 2.0f, pos.y + 8.0f);

    if (is_selected && motor_on)
    {
      // Active: bright green LED with glow
      draw_list->AddCircleFilled(led_center, led_radius, 0xFF00FF00);
      draw_list->AddCircle(led_center, led_radius + 2.0f, 0x4000FF00, 0, 2.0f);
    }
    else if (has_disk)
    {
      // Disk inserted but not active: dim green
      draw_list->AddCircleFilled(led_center, led_radius, 0xFF006600);
    }
    else
    {
      // Empty: dark
      draw_list->AddCircleFilled(led_center, led_radius, 0xFF333333);
    }
    draw_list->AddCircle(led_center, led_radius, 0xFF666666, 0, 1.0f);

    // Drive label after LED
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + led_radius * 2 + 8.0f);
    ImGui::TextColored(is_selected ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                       "D%d", drive + 1);

    if (has_disk && image)
    {
      // Filename on same line
      ImGui::SameLine();
      std::string filename = getFilename(image->getFilepath());
      // Truncate long filenames
      if (filename.length() > 20)
      {
        filename = filename.substr(0, 17) + "...";
      }
      ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", filename.c_str());

      // Status icons on same line
      if (image->isWriteProtected())
      {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "[WP]");
      }

      // Eject button at end of line
      ImGui::SameLine(ImGui::GetWindowWidth() - 45.0f);
      if (ImGui::SmallButton("Eject"))
      {
        if (eject_disk_callback_)
        {
          eject_disk_callback_(drive);
        }
        // IMPORTANT: After eject, disk image is freed - must exit immediately
        // to avoid accessing freed memory (use-after-free bug)
        ImGui::EndChild();
        if (is_selected)
        {
          ImGui::PopStyleColor(2);
        }
        ImGui::PopID();
        return;
      }

      // Second line: format info
      std::string format_info;
      if (woz_image)
      {
        format_info = image->getFormatName() + " " + woz_image->getDiskTypeString();
        std::string boot_fmt = woz_image->getBootSectorFormatString();
        if (boot_fmt != "Unknown")
        {
          format_info += " | " + boot_fmt;
        }
      }
      else
      {
        format_info = image->getFormatName();
      }
      ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.5f, 1.0f), "%s", format_info.c_str());

      // Third line: creator (if available, on same line)
      if (woz_image)
      {
        std::string creator = woz_image->getCreator();
        if (!creator.empty() && creator.length() < 30)
        {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "| %s", creator.c_str());
        }
      }
    }
    else
    {
      // Empty drive - buttons on same line
      ImGui::SameLine();
      if (ImGui::SmallButton("Load..."))
      {
        pending_drive_ = drive;
        file_browser_->open();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("New..."))
      {
        pending_drive_ = drive;
        if (save_file_browser_)
        {
          save_file_browser_->open();
        }
      }
    }
  }
  ImGui::EndChild();

  if (is_selected)
  {
    ImGui::PopStyleColor(2);
  }

  ImGui::PopID();
}

void disk_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

  if (ImGui::Begin("Disk II Controller", &open_))
  {
    // Gather all state
    bool motor_on = motor_on_callback_ ? motor_on_callback_() : false;
    bool q6 = q6_callback_ ? q6_callback_() : false;
    bool q7 = q7_callback_ ? q7_callback_() : false;
    bool write_mode = write_mode_callback_ ? write_mode_callback_() : false;
    int quarter_track = quarter_track_callback_ ? quarter_track_callback_() : -1;
    uint8_t phases = phase_states_callback_ ? phase_states_callback_() : 0;
    uint8_t data_latch = data_latch_callback_ ? data_latch_callback_() : 0;
    int selected_drive = selected_drive_callback_ ? selected_drive_callback_() : 0;

    // ===== COMPACT STATUS BAR =====
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Status row with icons
    float icon_size = 5.0f;
    float x_offset = 0.0f;

    // Motor indicator (circle LED)
    ImVec2 motor_center(pos.x + icon_size + 2.0f + x_offset, pos.y + 8.0f);
    if (motor_on)
    {
      draw_list->AddCircleFilled(motor_center, icon_size, 0xFF00DD00);
      draw_list->AddCircle(motor_center, icon_size + 2.0f, 0x4000DD00, 0, 2.0f);
    }
    else
    {
      draw_list->AddCircleFilled(motor_center, icon_size, 0xFF333333);
    }
    draw_list->AddCircle(motor_center, icon_size, 0xFF666666, 0, 1.0f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon_size * 2 + 8.0f);
    ImGui::TextColored(motor_on ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "MTR");

    // Read/Write mode indicator
    ImGui::SameLine();
    if (write_mode)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "WR");
    }
    else if (motor_on)
    {
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RD");
    }
    else
    {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "--");
    }

    // Q6/Q7 latch states
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Q6:%d Q7:%d", q6 ? 1 : 0, q7 ? 1 : 0);

    // Drive indicator
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "D%d", selected_drive + 1);

    // ===== TRACK & DATA ROW =====
    // Track position with visual bar
    int track = quarter_track >= 0 ? quarter_track / 4 : -1;
    int quarter = quarter_track >= 0 ? quarter_track % 4 : 0;

    if (track >= 0)
    {
      if (quarter == 0)
      {
        ImGui::Text("T:%02d", track);
      }
      else
      {
        ImGui::Text("T:%02d.%d", track, quarter * 25);
      }
    }
    else
    {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "T:--");
    }

    // Track bar visualization
    ImGui::SameLine();
    pos = ImGui::GetCursorScreenPos();
    float bar_width = 100.0f;
    float bar_height = 10.0f;

    // Background bar
    draw_list->AddRectFilled(pos, ImVec2(pos.x + bar_width, pos.y + bar_height),
                             0xFF333333, 2.0f);

    // Track position indicator
    if (quarter_track >= 0)
    {
      float track_pos = (static_cast<float>(quarter_track) / 140.0f) * bar_width;
      draw_list->AddRectFilled(ImVec2(pos.x + track_pos - 2, pos.y),
                               ImVec2(pos.x + track_pos + 2, pos.y + bar_height),
                               0xFF00AAFF, 1.0f);
    }

    // Track tick marks (every 5 tracks)
    for (int t = 0; t <= 35; t += 5)
    {
      float tick_x = pos.x + (static_cast<float>(t * 4) / 140.0f) * bar_width;
      draw_list->AddLine(ImVec2(tick_x, pos.y + bar_height - 2),
                         ImVec2(tick_x, pos.y + bar_height),
                         0xFF666666, 1.0f);
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height));

    // Data latch value
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "$%02X", data_latch);

    // ===== STEPPER PHASES (compact inline) =====
    ImGui::Text("PH:");
    ImGui::SameLine();

    pos = ImGui::GetCursorScreenPos();
    float phase_radius = 5.0f;
    float phase_spacing = 18.0f;

    for (int i = 0; i < 4; i++)
    {
      bool phase_on = (phases >> i) & 0x01;
      ImVec2 center(pos.x + phase_radius + 2.0f + (i * phase_spacing), pos.y + 6.0f);

      uint32_t color = phase_on ? 0xFF00CFFF : 0xFF333333;  // Orange when on
      draw_list->AddCircleFilled(center, phase_radius, color);
      draw_list->AddCircle(center, phase_radius, 0xFF555555, 0, 1.0f);

      if (phase_on)
      {
        draw_list->AddCircle(center, phase_radius + 1.5f, 0x4000CFFF, 0, 1.5f);
      }
    }
    ImGui::Dummy(ImVec2(4 * phase_spacing + 4, phase_radius * 2 + 4));

    // Phase labels
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "0123");

    // Phase value as hex
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "=$%X", phases);

    ImGui::Separator();

    // ===== DRIVE PANELS =====
    renderDrivePanel(0);
    renderDrivePanel(1);
  }
  ImGui::End();

  ImGui::PopStyleVar(2);

  // Render file browser dialogs
  if (file_browser_)
  {
    file_browser_->render();
  }

  if (save_file_browser_)
  {
    save_file_browser_->render();
  }
}
