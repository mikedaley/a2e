#include "utils/logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>

Logger &Logger::instance()
{
  static Logger instance;
  return instance;
}

Logger::Logger()
{
  // deque doesn't support reserve, but that's fine - it grows efficiently
}

void Logger::log(LogLevel level, const std::string &message)
{
  LogEntry entry;
  entry.level = level;
  entry.message = message;
  entry.timestamp = std::chrono::system_clock::now();

  {
    std::lock_guard<std::mutex> lock(mutex_);

    entries_.push_back(std::move(entry));
    ++total_count_;

    // Remove oldest entries if we exceed the limit
    while (entries_.size() > max_entries_)
    {
      entries_.pop_front();
    }
  }

  // Echo to console if enabled
  if (echo_to_console_)
  {
    auto &stream = (level == LogLevel::ERROR || level == LogLevel::WARNING)
                       ? std::cerr
                       : std::cout;
    stream << "[" << levelToString(level) << "] " << message << std::endl;
  }
}

void Logger::logVA(LogLevel level, const char *format, va_list args)
{
  char buffer[4096];
  vsnprintf(buffer, sizeof(buffer), format, args);
  log(level, buffer);
}

void Logger::logf(LogLevel level, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logVA(level, format, args);
  va_end(args);
}

void Logger::debugf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logVA(LogLevel::DEBUG, format, args);
  va_end(args);
}

void Logger::infof(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logVA(LogLevel::INFO, format, args);
  va_end(args);
}

void Logger::warningf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logVA(LogLevel::WARNING, format, args);
  va_end(args);
}

void Logger::errorf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logVA(LogLevel::ERROR, format, args);
  va_end(args);
}

std::deque<LogEntry> Logger::getEntries() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_;
}

std::pair<std::deque<LogEntry>, size_t> Logger::getNewEntries(size_t last_count) const
{
  std::lock_guard<std::mutex> lock(mutex_);

  std::deque<LogEntry> new_entries;

  if (total_count_ > last_count)
  {
    // Calculate how many new entries we have
    size_t new_count = total_count_ - last_count;

    // But we can only return what's still in the buffer
    size_t available = std::min(new_count, entries_.size());

    // Get the newest entries
    auto start = entries_.end() - available;
    new_entries.assign(start, entries_.end());
  }

  return {new_entries, total_count_};
}

size_t Logger::getTotalCount() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return total_count_;
}

void Logger::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  // Don't reset total_count_ - it's used for change detection
}

void Logger::setMaxEntries(size_t max_entries)
{
  std::lock_guard<std::mutex> lock(mutex_);
  max_entries_ = max_entries;

  // Trim if necessary
  while (entries_.size() > max_entries_)
  {
    entries_.pop_front();
  }
}

const char *Logger::levelToString(LogLevel level)
{
  switch (level)
  {
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARNING:
    return "WARN";
  case LogLevel::ERROR:
    return "ERROR";
  default:
    return "???";
  }
}

uint32_t Logger::levelToColor(LogLevel level)
{
  // ABGR format for ImGui
  switch (level)
  {
  case LogLevel::DEBUG:
    return 0xFF808080;  // Gray
  case LogLevel::INFO:
    return 0xFFFFFFFF;  // White
  case LogLevel::WARNING:
    return 0xFF00FFFF;  // Yellow
  case LogLevel::ERROR:
    return 0xFF5050FF;  // Red
  default:
    return 0xFFFFFFFF;
  }
}
