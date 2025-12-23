#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * DiskImage - Handles disk image loading and nibble conversion
 *
 * Supports .dsk/.do format (143,360 bytes = 35 tracks x 16 sectors x 256 bytes)
 * Converts to nibble format for Disk II controller emulation using 6-and-2 GCR encoding.
 */
class DiskImage
{
public:
  // Disk geometry constants
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int DSK_IMAGE_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR; // 143,360

  // Nibble track constants
  static constexpr int NIBBLES_PER_TRACK = 6656; // Standard nibble track size

  DiskImage();
  ~DiskImage() = default;

  /**
   * Load a .dsk/.do disk image from file
   * @param filepath Path to disk image file
   * @return true on success
   */
  bool load(const std::string &filepath);

  /**
   * Check if a disk image is loaded
   */
  bool isLoaded() const { return loaded_; }

  /**
   * Get the filepath of the loaded image
   */
  const std::string &getFilepath() const { return filepath_; }

  /**
   * Get a nibble from the specified track at the given position
   * @param track Track number (0-34)
   * @param position Byte position within nibble track
   * @return Nibble byte value
   */
  uint8_t getNibble(int track, int position) const;

  /**
   * Get the size of a nibble track
   */
  int getNibbleTrackSize() const { return NIBBLES_PER_TRACK; }

  /**
   * Check if image is write-protected
   */
  bool isWriteProtected() const { return write_protected_; }

  /**
   * Set write protection status
   */
  void setWriteProtected(bool wp) { write_protected_ = wp; }

private:
  /**
   * Convert raw sector data to nibble format for all tracks
   */
  void nibblizeAllTracks();

  /**
   * Nibblize a single track
   * @param track Track number to nibblize
   */
  void nibblizeTrack(int track);

  /**
   * Get raw sector data pointer
   * @param track Track number (0-34)
   * @param sector Logical sector number (0-15)
   * @return Pointer to 256 bytes of sector data
   */
  const uint8_t *getSectorData(int track, int sector) const;

  // 6-and-2 encoding translate table (64 valid disk nibbles)
  static const uint8_t TRANS62[64];
  
  // DOS 3.3 physical to logical sector mapping (from apple2js)
  static const uint8_t DOS_PHYSICAL_TO_LOGICAL[16];

  // Raw sector data from .dsk file
  std::array<uint8_t, DSK_IMAGE_SIZE> raw_data_;

  // Pre-nibblized track data for fast access
  std::array<std::vector<uint8_t>, TRACKS> nibble_tracks_;

  std::string filepath_;
  bool loaded_ = false;
  bool write_protected_ = true; // Start read-only for safety
  int volume_ = 254;            // Default DOS 3.3 volume number
};
