#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/**
 * File browser dialog mode
 */
enum class FileBrowserMode
{
  Open, // Select existing file
  Save  // Enter filename to save
};

/**
 * FileBrowserDialog - ImGui-based file browser for selecting files
 *
 * Provides a modal dialog for browsing the filesystem and selecting files.
 * Supports filtering by file extensions and both open/save modes.
 */
class FileBrowserDialog
{
public:
  /**
   * Callback type for when a file is selected
   */
  using SelectCallback = std::function<void(const std::string &)>;

  /**
   * Constructor
   * @param title Dialog title
   * @param extensions Vector of allowed extensions (e.g., {".woz", ".dsk"})
   * @param mode Open or Save mode (default: Open)
   */
  FileBrowserDialog(const std::string &title,
                    const std::vector<std::string> &extensions = {},
                    FileBrowserMode mode = FileBrowserMode::Open);

  /**
   * Open the dialog
   * @param start_path Initial directory to show
   */
  void open(const std::string &start_path = "");

  /**
   * Close the dialog without selecting
   */
  void close();

  /**
   * Check if the dialog is open
   */
  bool isOpen() const { return open_; }

  /**
   * Render the dialog (call each frame)
   */
  void render();

  /**
   * Set callback for when a file is selected
   */
  void setSelectCallback(SelectCallback callback);

  /**
   * Get the selected file path (empty if none)
   */
  const std::string &getSelectedPath() const { return selected_path_; }

  /**
   * Set the default filename for save mode
   * @param filename Default filename (e.g., "NewDisk.woz")
   */
  void setDefaultFilename(const std::string &filename);

  /**
   * Get the dialog mode
   */
  FileBrowserMode getMode() const { return mode_; }

  /**
   * Get the last accessed directory path (static, shared across all dialogs)
   * @return Last accessed directory path
   */
  static const std::string &getLastPath() { return last_path_; }

  /**
   * Set the last accessed directory path
   * @param path Directory path to remember
   */
  static void setLastPath(const std::string &path) { last_path_ = path; }

private:
  // Static last path shared across all dialog instances
  static std::string last_path_;
  struct FileEntry
  {
    std::string name;
    std::string full_path;
    bool is_directory;
    uintmax_t size;
  };

  std::string title_;
  std::vector<std::string> extensions_;
  SelectCallback select_callback_;
  FileBrowserMode mode_ = FileBrowserMode::Open;

  bool open_ = false;
  bool should_open_ = false;
  std::filesystem::path current_path_;
  std::vector<FileEntry> entries_;
  int selected_index_ = -1;
  std::string selected_path_;
  char path_buffer_[1024] = {0};

  // Save mode specific
  char filename_buffer_[256] = {0};
  std::string default_filename_;

  /**
   * Refresh the file listing for the current directory
   */
  void refreshDirectory();

  /**
   * Check if a file matches the extension filter
   */
  bool matchesFilter(const std::string &filename) const;

  /**
   * Navigate to a new directory
   */
  void navigateTo(const std::filesystem::path &path);

  /**
   * Format file size for display
   */
  static std::string formatSize(uintmax_t size);
};
