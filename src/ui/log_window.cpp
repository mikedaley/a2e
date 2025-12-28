#include "ui/log_window.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <sstream>

log_window::log_window()
{
  open_ = false;  // Start closed by default
}

void log_window::update(float /*deltaTime*/)
{
  if (!open_)
  {
    return;
  }

  // Check for new log entries
  size_t current_count = Logger::instance().getTotalCount();
  if (current_count != last_total_count_)
  {
    // Refresh the cache
    cached_entries_ = Logger::instance().getEntries();
    last_total_count_ = current_count;

    // Scroll to bottom if auto-scroll is enabled
    if (auto_scroll_)
    {
      scroll_to_bottom_ = true;
    }
  }
}

void log_window::render()
{
  if (!open_)
  {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(getName(), &open_, ImGuiWindowFlags_MenuBar))
  {
    // Menu bar
    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu("Options"))
      {
        ImGui::MenuItem("Auto-scroll", nullptr, &auto_scroll_);
        ImGui::Separator();
        ImGui::MenuItem("Show Debug", nullptr, &show_debug_);
        ImGui::MenuItem("Show Info", nullptr, &show_info_);
        ImGui::MenuItem("Show Warnings", nullptr, &show_warning_);
        ImGui::MenuItem("Show Errors", nullptr, &show_error_);
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    // Toolbar
    if (ImGui::Button("Clear"))
    {
      Logger::instance().clear();
      cached_entries_.clear();
    }
    ImGui::SameLine();

    if (ImGui::Button("Copy All"))
    {
      std::string all_text;
      for (const auto &entry : cached_entries_)
      {
        if (passesFilter(entry))
        {
          all_text += "[" + formatTimestamp(entry.timestamp) + "] ";
          all_text += "[";
          all_text += Logger::levelToString(entry.level);
          all_text += "] ";
          all_text += entry.message;
          all_text += "\n";
        }
      }
      ImGui::SetClipboardText(all_text.c_str());
    }
    ImGui::SameLine();

    // Filter buttons (compact)
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, show_debug_ ? 0xFF808080 : 0xFF404040);
    if (ImGui::SmallButton("D"))
      show_debug_ = !show_debug_;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Debug messages");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, show_info_ ? 0xFFFFFFFF : 0xFF404040);
    if (ImGui::SmallButton("I"))
      show_info_ = !show_info_;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Info messages");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, show_warning_ ? 0xFF00FFFF : 0xFF404040);
    if (ImGui::SmallButton("W"))
      show_warning_ = !show_warning_;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Warning messages");
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, show_error_ ? 0xFF5050FF : 0xFF404040);
    if (ImGui::SmallButton("E"))
      show_error_ = !show_error_;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Error messages");
    ImGui::SameLine();

    // Text filter
    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##filter", "Search...", filter_text_, sizeof(filter_text_));

    ImGui::SameLine();
    ImGui::TextDisabled("(%zu entries)", cached_entries_.size());

    ImGui::Separator();

    // Log content area with scrolling
    ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Use clipper for efficient rendering of large logs
    ImGuiListClipper clipper;

    // First, build a filtered index list for the clipper
    std::vector<int> filtered_indices;
    filtered_indices.reserve(cached_entries_.size());
    for (int i = 0; i < static_cast<int>(cached_entries_.size()); ++i)
    {
      if (passesFilter(cached_entries_[i]))
      {
        filtered_indices.push_back(i);
      }
    }

    clipper.Begin(static_cast<int>(filtered_indices.size()));
    while (clipper.Step())
    {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
      {
        int entry_idx = filtered_indices[row];
        const auto &entry = cached_entries_[entry_idx];

        // Timestamp
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]",
                           formatTimestamp(entry.timestamp).c_str());
        ImGui::SameLine();

        // Level indicator
        uint32_t color = Logger::levelToColor(entry.level);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "[%s]",
                           Logger::levelToString(entry.level));
        ImGui::SameLine();

        // Message
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s",
                           entry.message.c_str());
      }
    }
    clipper.End();

    // Auto-scroll to bottom
    if (scroll_to_bottom_ || (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
    {
      ImGui::SetScrollHereY(1.0f);
      scroll_to_bottom_ = false;
    }

    ImGui::EndChild();
  }
  ImGui::End();
}

bool log_window::passesFilter(const LogEntry &entry) const
{
  // Level filter
  switch (entry.level)
  {
  case LogLevel::DEBUG:
    if (!show_debug_)
      return false;
    break;
  case LogLevel::INFO:
    if (!show_info_)
      return false;
    break;
  case LogLevel::WARNING:
    if (!show_warning_)
      return false;
    break;
  case LogLevel::ERROR:
    if (!show_error_)
      return false;
    break;
  }

  // Text filter
  if (filter_text_[0] != '\0')
  {
    // Case-insensitive search
    std::string lower_msg = entry.message;
    std::string lower_filter = filter_text_;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(), ::tolower);
    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);

    if (lower_msg.find(lower_filter) == std::string::npos)
    {
      return false;
    }
  }

  return true;
}

std::string log_window::formatTimestamp(const std::chrono::system_clock::time_point &tp)
{
  auto time_t = std::chrono::system_clock::to_time_t(tp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()) %
            1000;

  std::tm tm;
  localtime_r(&time_t, &tm);

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
      << std::setw(2) << tm.tm_min << ":"
      << std::setw(2) << tm.tm_sec << "."
      << std::setw(3) << ms.count();

  return oss.str();
}
