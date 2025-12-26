#include "ui/file_browser_dialog.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>

FileBrowserDialog::FileBrowserDialog(const std::string &title,
                                     const std::vector<std::string> &extensions)
    : title_(title), extensions_(extensions)
{
}

void FileBrowserDialog::open(const std::string &start_path)
{
  if (start_path.empty())
  {
    current_path_ = std::filesystem::current_path();
  }
  else
  {
    current_path_ = start_path;
    if (!std::filesystem::is_directory(current_path_))
    {
      current_path_ = current_path_.parent_path();
    }
  }

  refreshDirectory();
  selected_index_ = -1;
  selected_path_.clear();
  should_open_ = true;
  open_ = true;

  // Update path buffer
  std::string path_str = current_path_.string();
  std::strncpy(path_buffer_, path_str.c_str(), sizeof(path_buffer_) - 1);
  path_buffer_[sizeof(path_buffer_) - 1] = '\0';
}

void FileBrowserDialog::close()
{
  open_ = false;
  should_open_ = false;
}

void FileBrowserDialog::setSelectCallback(SelectCallback callback)
{
  select_callback_ = std::move(callback);
}

void FileBrowserDialog::refreshDirectory()
{
  entries_.clear();

  try
  {
    // Add parent directory entry if not at root
    if (current_path_.has_parent_path() && current_path_ != current_path_.root_path())
    {
      FileEntry parent;
      parent.name = "..";
      parent.full_path = current_path_.parent_path().string();
      parent.is_directory = true;
      parent.size = 0;
      entries_.push_back(parent);
    }

    // Iterate directory contents
    for (const auto &entry : std::filesystem::directory_iterator(current_path_))
    {
      // Skip hidden files (starting with .)
      std::string filename = entry.path().filename().string();
      if (!filename.empty() && filename[0] == '.')
      {
        continue;
      }

      FileEntry fe;
      fe.name = filename;
      fe.full_path = entry.path().string();
      fe.is_directory = entry.is_directory();

      if (fe.is_directory)
      {
        fe.size = 0;
        entries_.push_back(fe);
      }
      else if (matchesFilter(filename))
      {
        try
        {
          fe.size = entry.file_size();
        }
        catch (...)
        {
          fe.size = 0;
        }
        entries_.push_back(fe);
      }
    }

    // Sort: directories first, then alphabetically
    std::sort(entries_.begin(), entries_.end(),
              [](const FileEntry &a, const FileEntry &b)
              {
                if (a.name == "..") return true;
                if (b.name == "..") return false;
                if (a.is_directory != b.is_directory)
                {
                  return a.is_directory;
                }
                return a.name < b.name;
              });
  }
  catch (const std::filesystem::filesystem_error &)
  {
    // Failed to read directory - try parent
    if (current_path_.has_parent_path())
    {
      current_path_ = current_path_.parent_path();
      refreshDirectory();
    }
  }
}

bool FileBrowserDialog::matchesFilter(const std::string &filename) const
{
  if (extensions_.empty())
  {
    return true; // No filter - show all files
  }

  std::string lower_name = filename;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

  for (const auto &ext : extensions_)
  {
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

    if (lower_name.size() >= lower_ext.size())
    {
      if (lower_name.compare(lower_name.size() - lower_ext.size(),
                             lower_ext.size(), lower_ext) == 0)
      {
        return true;
      }
    }
  }

  return false;
}

void FileBrowserDialog::navigateTo(const std::filesystem::path &path)
{
  if (std::filesystem::is_directory(path))
  {
    current_path_ = path;
    refreshDirectory();
    selected_index_ = -1;
    selected_path_.clear();

    // Update path buffer
    std::string path_str = current_path_.string();
    std::strncpy(path_buffer_, path_str.c_str(), sizeof(path_buffer_) - 1);
    path_buffer_[sizeof(path_buffer_) - 1] = '\0';
  }
}

std::string FileBrowserDialog::formatSize(uintmax_t size)
{
  if (size < 1024)
  {
    return std::to_string(size) + " B";
  }
  else if (size < 1024 * 1024)
  {
    return std::to_string(size / 1024) + " KB";
  }
  else
  {
    return std::to_string(size / (1024 * 1024)) + " MB";
  }
}

void FileBrowserDialog::render()
{
  if (!open_)
  {
    return;
  }

  if (should_open_)
  {
    ImGui::OpenPopup(title_.c_str());
    should_open_ = false;
  }

  // Center the modal
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

  if (ImGui::BeginPopupModal(title_.c_str(), &open_,
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse))
  {
    // Path input
    ImGui::Text("Path:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("##path", path_buffer_, sizeof(path_buffer_),
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
      std::filesystem::path new_path(path_buffer_);
      if (std::filesystem::exists(new_path))
      {
        navigateTo(new_path);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Go"))
    {
      std::filesystem::path new_path(path_buffer_);
      if (std::filesystem::exists(new_path))
      {
        navigateTo(new_path);
      }
    }

    ImGui::Separator();

    // File listing
    ImVec2 list_size(-1, -ImGui::GetFrameHeightWithSpacing() * 2 - 10);
    if (ImGui::BeginChild("FileList", list_size, ImGuiChildFlags_Borders))
    {
      // Column headers
      ImGui::Columns(2, "FileColumns");
      ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() - 100);
      ImGui::Text("Name");
      ImGui::NextColumn();
      ImGui::Text("Size");
      ImGui::NextColumn();
      ImGui::Separator();

      for (int i = 0; i < static_cast<int>(entries_.size()); i++)
      {
        const auto &entry = entries_[i];

        // Build display name
        std::string display_name = entry.is_directory
                                       ? "[" + entry.name + "]"
                                       : entry.name;

        bool is_selected = (i == selected_index_);
        if (ImGui::Selectable(display_name.c_str(), is_selected,
                              ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
          selected_index_ = i;
          if (!entry.is_directory)
          {
            selected_path_ = entry.full_path;
          }
          else
          {
            selected_path_.clear();
          }

          // Double-click to navigate or select
          if (ImGui::IsMouseDoubleClicked(0))
          {
            if (entry.is_directory)
            {
              navigateTo(entry.full_path);
            }
            else
            {
              // Select file and close
              if (select_callback_)
              {
                select_callback_(entry.full_path);
              }
              close();
              ImGui::CloseCurrentPopup();
            }
          }
        }
        ImGui::NextColumn();

        // Size column
        if (!entry.is_directory)
        {
          ImGui::Text("%s", formatSize(entry.size).c_str());
        }
        else
        {
          ImGui::Text("");
        }
        ImGui::NextColumn();
      }

      ImGui::Columns(1);
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Selected file display
    ImGui::Text("Selected: %s",
                selected_path_.empty() ? "(none)" : selected_path_.c_str());

    // Buttons
    float button_width = 100;
    float spacing = 10;
    float total_width = button_width * 2 + spacing;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - total_width) / 2);

    bool has_selection = !selected_path_.empty();
    if (!has_selection)
    {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Select", ImVec2(button_width, 0)))
    {
      if (select_callback_ && !selected_path_.empty())
      {
        select_callback_(selected_path_);
      }
      close();
      ImGui::CloseCurrentPopup();
    }
    if (!has_selection)
    {
      ImGui::EndDisabled();
    }

    ImGui::SameLine(0, spacing);
    if (ImGui::Button("Cancel", ImVec2(button_width, 0)))
    {
      close();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
  else
  {
    // Popup was closed (e.g., by clicking outside or pressing Escape)
    open_ = false;
  }
}
