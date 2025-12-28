#pragma once

#include <string>
#include <deque>
#include <mutex>
#include <chrono>
#include <functional>
#include <cstdarg>

/**
 * Log level for categorizing messages
 */
enum class LogLevel
{
  DEBUG,
  INFO,
  WARNING,
  ERROR
};

/**
 * A single log entry
 */
struct LogEntry
{
  LogLevel level;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
};

/**
 * Logger - Thread-safe singleton logger with circular buffer
 *
 * Captures log messages and stores them in a circular buffer for display.
 * Designed to handle large amounts of logging efficiently.
 */
class Logger
{
public:
  /**
   * Get the singleton instance
   */
  static Logger &instance();

  /**
   * Log a message at the specified level
   * @param level Log level
   * @param message Message to log
   */
  void log(LogLevel level, const std::string &message);

  /**
   * Log a formatted message (printf-style)
   * @param level Log level
   * @param format Format string
   * @param ... Format arguments
   */
  void logf(LogLevel level, const char *format, ...);

  /**
   * Convenience methods for each log level
   */
  void debug(const std::string &message) { log(LogLevel::DEBUG, message); }
  void info(const std::string &message) { log(LogLevel::INFO, message); }
  void warning(const std::string &message) { log(LogLevel::WARNING, message); }
  void error(const std::string &message) { log(LogLevel::ERROR, message); }

  /**
   * Formatted convenience methods
   */
  void debugf(const char *format, ...);
  void infof(const char *format, ...);
  void warningf(const char *format, ...);
  void errorf(const char *format, ...);

  /**
   * Get all log entries (thread-safe copy)
   * @return Copy of current log entries
   */
  std::deque<LogEntry> getEntries() const;

  /**
   * Get entries added since last call (for efficient UI updates)
   * @param last_count The count from the previous call
   * @return Number of new entries and the new total count
   */
  std::pair<std::deque<LogEntry>, size_t> getNewEntries(size_t last_count) const;

  /**
   * Get total number of entries ever logged (for change detection)
   */
  size_t getTotalCount() const;

  /**
   * Clear all log entries
   */
  void clear();

  /**
   * Set maximum number of entries to keep
   * @param max_entries Maximum entries (default: 10000)
   */
  void setMaxEntries(size_t max_entries);

  /**
   * Get maximum entries setting
   */
  size_t getMaxEntries() const { return max_entries_; }

  /**
   * Enable/disable echo to stdout/stderr
   * @param enabled true to echo to console
   */
  void setEchoToConsole(bool enabled) { echo_to_console_ = enabled; }

  /**
   * Check if console echo is enabled
   */
  bool isEchoToConsole() const { return echo_to_console_; }

  /**
   * Convert log level to string
   */
  static const char *levelToString(LogLevel level);

  /**
   * Get color for log level (ImGui ABGR format)
   */
  static uint32_t levelToColor(LogLevel level);

private:
  Logger();
  ~Logger() = default;
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  void logVA(LogLevel level, const char *format, va_list args);

  mutable std::mutex mutex_;
  std::deque<LogEntry> entries_;
  size_t max_entries_ = 10000;
  size_t total_count_ = 0;  // Total entries ever added (for change detection)
  bool echo_to_console_ = true;
};

// Convenience macros for logging
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)

#define LOG_DEBUGF(...) Logger::instance().debugf(__VA_ARGS__)
#define LOG_INFOF(...) Logger::instance().infof(__VA_ARGS__)
#define LOG_WARNINGF(...) Logger::instance().warningf(__VA_ARGS__)
#define LOG_ERRORF(...) Logger::instance().errorf(__VA_ARGS__)
