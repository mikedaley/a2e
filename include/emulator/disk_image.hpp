#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <random>

/**
 * DiskImage - Handles disk image loading and bitstream/nibble data
 *
 * Supports multiple formats:
 * - .dsk/.do format (143,360 bytes = 35 tracks x 16 sectors x 256 bytes)
 *   Converts to nibble format for Disk II controller emulation using 6-and-2 GCR encoding.
 * - .woz (WOZ2) format - Chunk-based bitstream format with accurate copy protection
 */
class DiskImage
{
public:
  // Disk geometry constants for DSK format
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int DSK_IMAGE_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR; // 143,360

  // Nibble track constants
  static constexpr int NIBBLES_PER_TRACK = 6656; // Standard nibble track size for DSK

  // WOZ2 constants
  static constexpr int WOZ2_MAX_TRACKS = 160; // Support for 160 quarter-track entries (0.00-35.75 for 5.25")

  enum class Format
  {
    Unknown,
    DSK,
    WOZ2
  };

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
   * Get a nibble from the specified quarter-track at the given position
   * Quarter-tracks allow 0.25 track resolution (0-139 for 35 tracks)
   * For DSK format, uses nearest whole track
   * For WOZ2 format, uses TMAP to find the correct track data
   * @param quarter_track Quarter-track position (0-139)
   * @param position Byte/nibble position within track
   * @return Nibble byte value
   */
  uint8_t getNibbleAtQuarterTrack(int quarter_track, int position) const;

  /**
   * Get the size of a nibble/bit track for the current format
   * @param track Track number (0-34)
   */
  int getNibbleTrackSize(int track = 0) const;

  /**
   * Get the size of a nibble/bit track at the specified quarter-track
   * @param quarter_track Quarter-track position (0-139)
   */
  int getNibbleTrackSizeAtQuarterTrack(int quarter_track) const;

  /**
   * Check if image is write-protected
   */
  bool isWriteProtected() const { return write_protected_; }

  /**
   * Set write protection status
   */
  void setWriteProtected(bool wp) { write_protected_ = wp; }

  /**
   * Get the format of the loaded image
   */
  Format getFormat() const { return format_; }

  /**
   * Find which sector is at or before the given nibble position
   * Scans backwards to find the last sector address field
   * @param track Track number (0-34)
   * @param position Nibble position within track
   * @return Sector number (0-15) or -1 if no sector found
   */
  int findSectorAtPosition(int track, int position) const;

private:
  // === DSK Format Methods ===

  /**
   * Load a DSK format disk image
   */
  bool loadDSK(const std::vector<uint8_t> &data);

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

  // === WOZ2 Format Methods ===

  /**
   * Load a WOZ2 format disk image
   */
  bool loadWOZ2(const std::vector<uint8_t> &data);

  /**
   * Parse WOZ2 INFO chunk
   */
  bool parseINFO(const uint8_t *chunk_data, uint32_t chunk_size);

  /**
   * Parse WOZ2 TMAP chunk
   */
  bool parseTMAP(const uint8_t *chunk_data, uint32_t chunk_size);

  /**
   * Parse WOZ2 TRKS chunk
   */
  bool parseTRKS(const uint8_t *chunk_data, uint32_t chunk_size);

  /**
   * Calculate CRC32 for WOZ2 validation (Gary S. Brown's algorithm)
   */
  uint32_t calculateCRC32(const uint8_t *data, size_t length) const;

  /**
   * Get a bit from the WOZ2 bitstream at the given track and bit position
   */
  uint8_t getBitFromWOZ2(int track, int bit_position) const;

  /**
   * MC3470 emulation: generate random bit for weak bit protection
   */
  uint8_t getWeakBit() const;

  // === WOZ2 Data Structures ===

  struct WOZ2Info
  {
    uint8_t version = 0;
    uint8_t disk_type = 0;         // 1=5.25", 2=3.5"
    bool write_protected = false;
    bool synchronized = false;
    bool cleaned = false;
    std::string creator;
    uint8_t disk_sides = 1;
    uint8_t boot_sector_format = 0;
    uint8_t optimal_bit_timing = 32; // 32 = 4µs for 5.25"
    uint16_t compatible_hardware = 0;
    uint16_t required_ram = 0;
    uint16_t largest_track = 0;     // In 512-byte blocks
  };

  struct WOZ2Track
  {
    uint16_t starting_block = 0;  // Relative file block offset
    uint16_t block_count = 0;     // Number of 512-byte blocks
    uint32_t bit_count = 0;       // Total bits in track
    std::vector<uint8_t> data;    // Actual bitstream data
  };

  // 6-and-2 encoding translate table (64 valid disk nibbles) - for DSK format
  static const uint8_t TRANS62[64];

  // DOS 3.3 physical to logical sector mapping (from apple2js) - for DSK format
  static const uint8_t DOS_PHYSICAL_TO_LOGICAL[16];

  // === Common Data ===
  std::string filepath_;
  bool loaded_ = false;
  bool write_protected_ = true; // Start read-only for safety
  Format format_ = Format::Unknown;

  // === DSK Format Data ===
  std::array<uint8_t, DSK_IMAGE_SIZE> raw_data_;
  std::array<std::vector<uint8_t>, TRACKS> nibble_tracks_;
  int volume_ = 254; // Default DOS 3.3 volume number

  // === WOZ2 Format Data ===
  WOZ2Info woz2_info_;
  std::array<uint8_t, WOZ2_MAX_TRACKS> track_map_; // TMAP: maps quarter-track to track data index
  std::array<WOZ2Track, WOZ2_MAX_TRACKS> woz2_tracks_; // Track bitstream data

  // MC3470 weak bit emulation and self-sync reading
  mutable std::mt19937 rng_;
  mutable uint8_t mc3470_buffer_ = 0; // 8-bit shift register for self-sync reading
  mutable int mc3470_bit_count_ = 0;

  // Per-track bit position for WOZ2 reading
  mutable std::array<int, WOZ2_MAX_TRACKS> woz2_bit_positions_;
  mutable std::array<int, WOZ2_MAX_TRACKS> woz2_last_position_; // Track last position to know when to advance
};
