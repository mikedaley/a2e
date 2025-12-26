#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/**
 * FileBrowserDialog - ImGui-based file browser for selecting files
 *
 * Provides a modal dialog for browsing the filesystem and selecting files.
 * Supports filtering by file extensions.
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
   */
  FileBrowserDialog(const std::string &title,
                    const std::vector<std::string> &extensions = {});

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

private:
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

  bool open_ = false;
  bool should_open_ = false;
  std::filesystem::path current_path_;
  std::vector<FileEntry> entries_;
  int selected_index_ = -1;
  std::string selected_path_;
  char path_buffer_[1024] = {0};

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
