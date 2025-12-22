#include "ui/disk_window.hpp"
#include "emulator/emulator.hpp"
#include "emulator/disk_ii.hpp"
#include "emulator/disk_image.hpp"
#include <imgui.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

disk_window::disk_window(emulator& emu)
  : emu_(emu)
{
  // Start in home directory
  const char* home = std::getenv("HOME");
  if (home)
  {
    current_path_ = home;
  }
  else
  {
    current_path_ = ".";
  }
}

void disk_window::update(float deltaTime)
{
  // Update activity indicators
  const DiskII* disk = emu_.getDiskII();
  if (disk)
  {
    for (int d = 0; d < 2; d++)
    {
      bool motor_on = disk->isMotorOn() && disk->getSelectedDrive() == d;
      
      if (motor_on)
      {
        drive_activity_timer_[d] = 0.5f;  // Keep LED on for 0.5s after activity
      }
      else if (drive_activity_timer_[d] > 0)
      {
        drive_activity_timer_[d] -= deltaTime;
      }
      
      last_motor_state_[d] = motor_on;
    }
  }
}

void disk_window::render()
{
  if (!open_) return;

  ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
  
  if (ImGui::Begin("Disk Drives", &open_))
  {
    renderDrivePanel(0);
    ImGui::Separator();
    renderDrivePanel(1);
  }
  ImGui::End();

  // Render file dialog if open
  if (show_file_dialog_)
  {
    renderFileDialog();
  }
}

void disk_window::renderDrivePanel(int drive)
{
  const DiskII* disk = emu_.getDiskII();
  if (!disk) return;

  ImGui::PushID(drive);

  // Drive header with activity LED
  bool is_active = drive_activity_timer_[drive] > 0;
  ImVec4 led_color = is_active ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
  
  ImGui::TextColored(led_color, "[*]");
  ImGui::SameLine();
  ImGui::Text("Drive %d", drive + 1);

  // Disk info
  bool has_disk = disk->isDiskInserted(drive);
  
  if (has_disk)
  {
    const DiskImage* image = disk->getDiskImage(drive);
    if (image)
    {
      std::string filename = getFilenameFromPath(image->getFilePath());
      ImGui::Text("Disk: %s", filename.c_str());
    }
    else
    {
      ImGui::Text("Disk: (inserted)");
    }

    // Track info (only for selected drive when motor is on)
    if (disk->getSelectedDrive() == drive)
    {
      ImGui::Text("Track: %d (half-track: %d)", 
                  disk->getCurrentTrack(), 
                  disk->getCurrentHalfTrack());
    }
    else
    {
      ImGui::Text("Track: --");
    }

    // Write protect status
    bool write_protected = disk->isWriteProtected(drive);
    ImGui::Text("Write Protect: %s", write_protected ? "ON" : "OFF");

    // Eject button
    if (ImGui::Button("Eject"))
    {
      emu_.ejectDisk(drive);
    }
    
    ImGui::SameLine();
    
    // Write protect toggle
    if (ImGui::Button(write_protected ? "Enable Write" : "Write Protect"))
    {
      // Note: Need non-const access to toggle write protect
      DiskII* disk_rw = emu_.getDiskII();
      if (disk_rw)
      {
        disk_rw->setWriteProtected(drive, !write_protected);
      }
    }
  }
  else
  {
    ImGui::TextDisabled("No disk inserted");
    ImGui::Text("Track: --");
    ImGui::Text("Write Protect: --");
    
    if (ImGui::Button("Insert Disk..."))
    {
      openFileDialog(drive);
    }
  }

  ImGui::PopID();
}

void disk_window::openFileDialog(int drive)
{
  file_dialog_drive_ = drive;
  show_file_dialog_ = true;
  selected_file_.clear();
  refreshDirectoryListing();
}

void disk_window::refreshDirectoryListing()
{
  directory_entries_.clear();

  try
  {
    // Add parent directory entry
    if (current_path_ != "/")
    {
      directory_entries_.push_back("..");
    }

    // Get directory contents
    std::vector<std::pair<std::string, bool>> entries;  // name, is_directory
    
    for (const auto& entry : fs::directory_iterator(current_path_))
    {
      std::string name = entry.path().filename().string();
      
      // Skip hidden files
      if (!name.empty() && name[0] == '.')
        continue;

      bool is_dir = entry.is_directory();
      
      // Only show directories and valid disk images
      if (is_dir || isValidDiskImage(name))
      {
        entries.push_back({name, is_dir});
      }
    }

    // Sort: directories first, then files, alphabetically
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      if (a.second != b.second)
        return a.second > b.second;  // Directories first
      return a.first < b.first;       // Then alphabetically
    });

    // Add to listing with directory markers
    for (const auto& [name, is_dir] : entries)
    {
      if (is_dir)
      {
        directory_entries_.push_back("[" + name + "]");
      }
      else
      {
        directory_entries_.push_back(name);
      }
    }
  }
  catch (const std::exception& e)
  {
    directory_entries_.push_back("(Error reading directory)");
  }
}

void disk_window::renderFileDialog()
{
  ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
  
  std::string title = "Select Disk Image - Drive " + std::to_string(file_dialog_drive_ + 1);
  
  if (ImGui::Begin(title.c_str(), &show_file_dialog_))
  {
    // Current path display
    ImGui::Text("Path: %s", current_path_.c_str());
    ImGui::Separator();

    // File list
    ImVec2 list_size(-1, -50);  // Leave room for buttons at bottom
    if (ImGui::BeginListBox("##files", list_size))
    {
      for (const auto& entry : directory_entries_)
      {
        bool is_selected = (entry == selected_file_);
        
        // Color directories differently
        bool is_directory = (!entry.empty() && entry[0] == '[') || entry == "..";
        
        if (is_directory)
        {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
        }

        if (ImGui::Selectable(entry.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
          selected_file_ = entry;

          // Handle double-click
          if (ImGui::IsMouseDoubleClicked(0))
          {
            if (entry == "..")
            {
              // Go up one directory
              fs::path p(current_path_);
              current_path_ = p.parent_path().string();
              selected_file_.clear();
              refreshDirectoryListing();
            }
            else if (is_directory)
            {
              // Enter directory (remove brackets)
              std::string dir_name = entry.substr(1, entry.length() - 2);
              current_path_ = (fs::path(current_path_) / dir_name).string();
              selected_file_.clear();
              refreshDirectoryListing();
            }
            else
            {
              // Select file
              std::string full_path = (fs::path(current_path_) / entry).string();
              if (emu_.insertDisk(file_dialog_drive_, full_path))
              {
                show_file_dialog_ = false;
              }
            }
          }
        }

        if (is_directory)
        {
          ImGui::PopStyleColor();
        }
      }
      ImGui::EndListBox();
    }

    ImGui::Separator();

    // Selected file display
    if (!selected_file_.empty() && selected_file_ != ".." && selected_file_[0] != '[')
    {
      ImGui::Text("Selected: %s", selected_file_.c_str());
    }
    else
    {
      ImGui::TextDisabled("No file selected");
    }

    // Buttons
    ImGui::SameLine(ImGui::GetWindowWidth() - 170);
    
    bool can_open = !selected_file_.empty() && 
                    selected_file_ != ".." && 
                    selected_file_[0] != '[';
    
    if (!can_open)
    {
      ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Open", ImVec2(75, 0)))
    {
      std::string full_path = (fs::path(current_path_) / selected_file_).string();
      if (emu_.insertDisk(file_dialog_drive_, full_path))
      {
        show_file_dialog_ = false;
      }
    }
    
    if (!can_open)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    
    if (ImGui::Button("Cancel", ImVec2(75, 0)))
    {
      show_file_dialog_ = false;
    }
  }
  ImGui::End();
}

bool disk_window::isValidDiskImage(const std::string& filename) const
{
  // Get lowercase extension
  size_t dot = filename.rfind('.');
  if (dot == std::string::npos)
    return false;

  std::string ext = filename.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Valid Apple II disk image extensions
  return ext == ".dsk" || ext == ".do" || ext == ".po";
}

std::string disk_window::getFilenameFromPath(const std::string& path) const
{
  return fs::path(path).filename().string();
}
