#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>

/**
 * preferences - Simple preference management for application settings
 *
 * Saves and loads preferences from a configuration file in the user's
 * home directory. Uses a simple key-value format.
 */
class preferences
{
public:
  /**
   * Construct preferences with a given application name
   * @param app_name Application name (used for config file path)
   */
  explicit preferences(const std::string& app_name);

  /**
   * Load preferences from disk
   * @return true if loaded successfully, false otherwise
   */
  bool load();

  /**
   * Save preferences to disk
   * @return true if saved successfully, false otherwise
   */
  bool save();

  /**
   * Get a boolean preference
   * @param key Preference key
   * @param default_value Default value if key doesn't exist
   * @return Preference value
   */
  bool getBool(const std::string& key, bool default_value = false) const;

  /**
   * Set a boolean preference
   * @param key Preference key
   * @param value Preference value
   */
  void setBool(const std::string& key, bool value);

  /**
   * Get an integer preference
   * @param key Preference key
   * @param default_value Default value if key doesn't exist
   * @return Preference value
   */
  int getInt(const std::string& key, int default_value = 0) const;

  /**
   * Set an integer preference
   * @param key Preference key
   * @param value Preference value
   */
  void setInt(const std::string& key, int value);

  /**
   * Get a string preference
   * @param key Preference key
   * @param default_value Default value if key doesn't exist
   * @return Preference value
   */
  std::string getString(const std::string& key, const std::string& default_value = "") const;

  /**
   * Set a string preference
   * @param key Preference key
   * @param value Preference value
   */
  void setString(const std::string& key, const std::string& value);

  /**
   * Check if a key exists
   * @param key Preference key
   * @return true if key exists
   */
  bool hasKey(const std::string& key) const;

private:
  std::filesystem::path config_path_;
  std::unordered_map<std::string, std::string> data_;
};

