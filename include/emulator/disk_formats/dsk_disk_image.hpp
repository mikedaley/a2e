#pragma once

#include "emulator/disk_image.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * DskDiskImage - DSK/DO/PO disk image format support
 *
 * DSK is a raw sector image format commonly used for Apple II disk images.
 * It stores 35 tracks × 16 sectors × 256 bytes = 143,360 bytes (140KB).
 *
 * There are two sector ordering schemes:
 * - DOS order (.dsk, .do): Sectors stored in DOS 3.3 logical order
 * - ProDOS order (.po): Sectors stored in ProDOS physical order
 *
 * This class converts the raw sector data to/from GCR-encoded nibbles
 * on-the-fly, simulating how a real Disk II controller reads/writes.
 */
class DskDiskImage : public DiskImage
{
public:
  // Disk geometry constants
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int TRACK_SIZE = SECTORS_PER_TRACK * BYTES_PER_SECTOR;  // 4096 bytes
  static constexpr int DISK_SIZE = TRACKS * TRACK_SIZE;  // 143360 bytes

  // Nibble track size (approximate, varies slightly per track)
  static constexpr int NIBBLES_PER_TRACK = 6656;

  DskDiskImage();
  ~DskDiskImage() override = default;

  // ===== Loading =====
  bool load(const std::string &filepath) override;
  bool isLoaded() const override { return loaded_; }
  const std::string &getFilepath() const override { return filepath_; }
  Format getFormat() const override { return format_; }

  // ===== Head Positioning =====
  void setPhase(int phase, bool on) override;
  int getQuarterTrack() const override { return quarter_track_; }
  int getTrack() const override { return quarter_track_ / 4; }

  // ===== Geometry =====
  int getTrackCount() const override { return TRACKS; }
  bool hasData() const override;

  // ===== Data Access =====
  void advanceBitPosition(uint64_t elapsed_cycles) override;
  uint8_t readNibble() override;

  // ===== Write Operations =====
  void writeNibble(uint8_t nibble) override;
  bool save() override;
  bool saveAs(const std::string &filepath) override;

  // ===== Status =====
  bool isWriteProtected() const override { return write_protected_; }
  std::string getFormatName() const override;

  // ===== DSK-specific =====

  /**
   * Check if this is a ProDOS-order image
   */
  bool isProDOSOrder() const { return format_ == Format::PO; }

  /**
   * Get the volume number (default 254)
   */
  uint8_t getVolumeNumber() const { return volume_number_; }

  /**
   * Set the volume number for address field encoding
   */
  void setVolumeNumber(uint8_t volume) { volume_number_ = volume; }

private:
  // Raw sector data storage
  std::array<uint8_t, DISK_SIZE> sector_data_{};

  // Nibblized track cache
  struct NibbleTrack
  {
    std::vector<uint8_t> nibbles;
    bool dirty = false;  // Track has been modified
    bool valid = false;  // Track has been nibblized
  };
  std::array<NibbleTrack, TRACKS> nibble_tracks_{};

  // State
  std::string filepath_;
  Format format_ = Format::Unknown;
  bool loaded_ = false;
  bool write_protected_ = false;
  bool modified_ = false;
  uint8_t volume_number_ = 254;

  // Head position (0-159 quarter-tracks)
  int quarter_track_ = 0;
  uint8_t phase_states_ = 0;  // Bit field of active phases
  int last_phase_ = 0;        // Last phase that was activated (for stepper direction)

  // Bit/nibble position within current track
  size_t nibble_position_ = 0;
  uint64_t last_cycle_count_ = 0;

  // Shift register for reading bits
  uint8_t shift_register_ = 0;

  // ===== Internal Methods =====

  /**
   * Detect disk format from content (ProDOS vs DOS order)
   * Checks for filesystem signatures rather than relying on file extension
   */
  Format detectFormat() const;

  /**
   * Nibblize a track from sector data
   */
  void nibblizeTrack(int track);

  /**
   * Denibblize a track back to sector data
   */
  void denibblizeTrack(int track);

  /**
   * Update head position based on phase magnet states
   */
  void updateHeadPosition(int phase);

  /**
   * Get the physical sector number for a logical sector
   * Handles DOS vs ProDOS ordering
   */
  int getPhysicalSector(int logical_sector) const;

  /**
   * Get the logical sector number for a physical sector
   */
  int getLogicalSector(int physical_sector) const;

  /**
   * Ensure the current track is nibblized
   */
  void ensureTrackNibblized();

  /**
   * Decode a 4-and-4 encoded byte pair
   */
  static uint8_t decode4and4(uint8_t odd, uint8_t even);

  /**
   * Decode 6-and-2 encoded data back to 256 bytes
   */
  static bool decode6and2(const uint8_t *encoded, uint8_t *output);

  /**
   * Find the next sector in the nibble stream
   * Returns the starting position of the address field, or -1 if not found
   */
  int findNextSector(int start_pos) const;
};
