#include "ui/disk_window.hpp"
#include "emulator/emulator.hpp"
#include <imgui.h>
#include <SDL3/SDL_dialog.h>
#include <filesystem>

disk_window::disk_window(emulator& emu)
{
  // Set up state callback to fetch disk information
  state_callback_ = [&emu]() -> disk_state {
    disk_state state{};
    auto* disk_ii = emu.getDiskII();
    if (!disk_ii)
      return state;

    // Fetch controller state
    state.motor_on = disk_ii->isMotorOn();
    state.selected_drive = disk_ii->getSelectedDrive();
    state.phase_mask = disk_ii->getPhaseMask();
    state.q6 = disk_ii->getQ6();
    state.q7 = disk_ii->getQ7();
    state.data_latch = disk_ii->getDataLatch();

    // Drive 0
    state.drive0_track = disk_ii->getCurrentTrack(0);
    state.drive0_track_position = disk_ii->getTrackPosition(0);
    state.drive0_nibble_pos = disk_ii->getNibblePosition(0);
    state.drive0_has_disk = disk_ii->hasDisk(0);
    if (const auto* img = disk_ii->getDiskImage(0))
    {
      state.drive0_filename = img->getFilepath();
      state.drive0_write_protected = img->isWriteProtected();
      state.drive0_sector = img->findSectorAtPosition(state.drive0_track, state.drive0_nibble_pos);
    }

    // Drive 1
    state.drive1_track = disk_ii->getCurrentTrack(1);
    state.drive1_track_position = disk_ii->getTrackPosition(1);
    state.drive1_nibble_pos = disk_ii->getNibblePosition(1);
    state.drive1_has_disk = disk_ii->hasDisk(1);
    if (const auto* img = disk_ii->getDiskImage(1))
    {
      state.drive1_filename = img->getFilepath();
      state.drive1_write_protected = img->isWriteProtected();
      state.drive1_sector = img->findSectorAtPosition(state.drive1_track, state.drive1_nibble_pos);
    }

    return state;
  };

  // Set up disk load callback
  load_disk_callback_ = [&emu](int drive, const std::string& path) -> bool {
    return emu.insertDisk(drive, path);
  };

  // Set up disk eject callback
  eject_disk_callback_ = [&emu](int drive) { emu.ejectDisk(drive); };
}

void disk_window::update(float deltaTime)
{
  if (!open_ || !state_callback_)
    return;

  // Fetch fresh state
  state_ = state_callback_();
}

void disk_window::render()
{
  if (!open_)
    return;

  ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_))
  {
    // Drive 1 section
    ImGui::SetNextItemOpen(drive1_section_open_, ImGuiCond_Once);
    if ((drive1_section_open_ = ImGui::CollapsingHeader("Drive 1")))
    {
      renderDriveInfo(0);
    }

    ImGui::Spacing();

    // Drive 2 section
    ImGui::SetNextItemOpen(drive2_section_open_, ImGuiCond_Once);
    if ((drive2_section_open_ = ImGui::CollapsingHeader("Drive 2")))
    {
      renderDriveInfo(1);
    }

    ImGui::Spacing();

    // Controller status section
    ImGui::SetNextItemOpen(controller_section_open_, ImGuiCond_Once);
    if ((controller_section_open_ = ImGui::CollapsingHeader("Controller Status")))
    {
      renderControllerState();
    }
  }
  ImGui::End();
}

void disk_window::renderDriveInfo(int drive)
{
  // Push unique ID for this drive's widgets
  ImGui::PushID(drive);

  bool has_disk = (drive == 0) ? state_.drive0_has_disk : state_.drive1_has_disk;
  float track_position = (drive == 0) ? state_.drive0_track_position : state_.drive1_track_position;
  int nibble_pos = (drive == 0) ? state_.drive0_nibble_pos : state_.drive1_nibble_pos;
  int sector = (drive == 0) ? state_.drive0_sector : state_.drive1_sector;
  std::string filename = (drive == 0) ? state_.drive0_filename : state_.drive1_filename;
  bool write_protected = (drive == 0) ? state_.drive0_write_protected : state_.drive1_write_protected;

  // Status
  ImGui::Text("Status:");
  ImGui::SameLine();
  if (has_disk)
  {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "DISK LOADED");
  }
  else
  {
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "NO DISK");
  }

  if (has_disk)
  {
    // Display filename (extract just the filename from path)
    ImGui::Text("File:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", getFilename(filename).c_str());

    // Track, sector, and nibble position
    ImGui::Text("Track: %.2f", track_position);
    if (sector >= 0)
    {
      ImGui::Text("Sector: %d (Physical)", sector);
    }
    else
    {
      ImGui::Text("Sector: --- (No sector)");
    }
    ImGui::Text("Nibble: %d / 6656", nibble_pos);

    // Progress bar for nibble position
    float progress = nibble_pos / 6656.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0), "");

    // Write protection
    ImGui::Text("Write Protected:");
    ImGui::SameLine();
    if (write_protected)
    {
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "YES");
    }
    else
    {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "NO");
    }

    // Load and Eject buttons
    if (ImGui::Button("Load Disk..."))
    {
      handleDiskLoad(drive);
    }
    ImGui::SameLine();
    if (ImGui::Button("Eject"))
    {
      handleDiskEject(drive);
    }
  }
  else
  {
    // Only show Load button when no disk
    if (ImGui::Button("Load Disk..."))
    {
      handleDiskLoad(drive);
    }
  }

  ImGui::PopID();
}

void disk_window::renderControllerState()
{
  // Motor status
  ImGui::Text("Motor:");
  ImGui::SameLine();
  if (state_.motor_on)
  {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s ON", "\xE2\x97\x8F"); // Bullet character
  }
  else
  {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s OFF", "\xE2\x97\x8B"); // Circle character
  }

  // Selected drive
  ImGui::Text("Selected: Drive %d", state_.selected_drive + 1);

  // Stepper motor phases
  ImGui::Text("Phases:");
  ImGui::SameLine();
  for (int i = 0; i < 4; i++)
  {
    bool phase_active = (state_.phase_mask & (1 << i)) != 0;
    if (phase_active)
    {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
    }
    else
    {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    }

    char label[8];
    snprintf(label, sizeof(label), "%d", i);
    ImGui::SmallButton(label);
    ImGui::PopStyleColor(3);

    if (i < 3)
      ImGui::SameLine();
  }

  // Q6/Q7 mode flags
  ImGui::Text("Mode:");
  ImGui::SameLine();
  ImGui::Text("Q6=%s  Q7=%s", state_.q6 ? "Load" : "Shift", state_.q7 ? "Write" : "Read");

  // Data latch
  ImGui::Text("Data Latch: $%02X", state_.data_latch);
}

void disk_window::handleDiskLoad(int drive)
{
  // Create filter for disk images
  static SDL_DialogFileFilter filter = {.name = "Disk Images", .pattern = "dsk;do;woz"};

  // Create context for callback
  auto* context = new disk_load_context{.load_callback = load_disk_callback_, .drive = drive};

  // Show file dialog (asynchronous)
  SDL_ShowOpenFileDialog(
      [](void* userdata, const char* const* filelist, int filter) {
        auto* ctx = static_cast<disk_load_context*>(userdata);
        if (filelist && filelist[0])
        {
          // User selected a file
          ctx->load_callback(ctx->drive, filelist[0]);
        }
        delete ctx;
      },
      context,  // userdata
      nullptr,  // window
      &filter,  // filters
      1,        // nfilters
      nullptr,  // default_location
      false     // allow_many
  );
}

void disk_window::handleDiskEject(int drive)
{
  if (eject_disk_callback_)
  {
    eject_disk_callback_(drive);
  }
}

std::string disk_window::getFilename(const std::string& path)
{
  std::filesystem::path p(path);
  return p.filename().string();
}

void disk_window::loadState(preferences& prefs)
{
  drive1_section_open_ = prefs.getBool("window.disk.drive1_open", true);
  drive2_section_open_ = prefs.getBool("window.disk.drive2_open", true);
  controller_section_open_ = prefs.getBool("window.disk.controller_open", true);
}

void disk_window::saveState(preferences& prefs)
{
  prefs.setBool("window.disk.drive1_open", drive1_section_open_);
  prefs.setBool("window.disk.drive2_open", drive2_section_open_);
  prefs.setBool("window.disk.controller_open", controller_section_open_);
}
