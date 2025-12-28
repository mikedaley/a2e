#pragma once

#include "ui/base_window.hpp"
#include "utils/logger.hpp"
#include <deque>
#include <string>

/**
 * log_window - Displays emulator log messages in a scrollable view
 *
 * Features:
 * - Efficient rendering using ImGui clipper for large logs
 * - Auto-scroll to bottom for new messages
 * - Filter by log level
 * - Clear button
 * - Copy to clipboard support
 */
class log_window : public base_window
{
public:
  log_window();

  void update(float deltaTime) override;
  void render() override;
  const char *getName() const override { return "Log"; }

private:
  // Cached entries for efficient rendering
  std::deque<LogEntry> cached_entries_;
  size_t last_total_count_ = 0;

  // UI state
  bool auto_scroll_ = true;
  bool scroll_to_bottom_ = false;
  bool show_debug_ = true;
  bool show_info_ = true;
  bool show_warning_ = true;
  bool show_error_ = true;

  // Filter text
  char filter_text_[256] = {0};

  // Check if entry passes current filters
  bool passesFilter(const LogEntry &entry) const;

  // Format timestamp for display
  static std::string formatTimestamp(const std::chrono::system_clock::time_point &tp);
};
