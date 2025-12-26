#pragma once

#include "emulator/disk_image.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * WozDiskImage - WOZ format disk image implementation
 *
 * WOZ is a bit-accurate disk image format that captures the exact
 * magnetic flux transitions on a 5.25" floppy disk. It supports:
 *
 * - Quarter-track positioning (160 positions for 40 tracks)
 * - Variable track lengths (accurate timing)
 * - Copy-protected disk preservation
 * - Write protection status
 *
 * File format reference: https://applesaucefdc.com/woz/
 *
 * This implementation supports both WOZ 1.0 and WOZ 2.0 formats.
 */
class WozDiskImage : public DiskImage
{
public:
  WozDiskImage();
  ~WozDiskImage() override = default;

  // Move operations
  WozDiskImage(WozDiskImage &&) = default;
  WozDiskImage &operator=(WozDiskImage &&) = default;

  // ===== DiskImage interface implementation =====

  bool load(const std::string &filepath) override;
  bool isLoaded() const override;
  const std::string &getFilepath() const override;
  Format getFormat() const override;

  int getTrackCount() const override;
  bool hasQuarterTrack(int quarter_track) const override;
  uint32_t getTrackBitCount(int quarter_track) const override;

  uint8_t readNibble(int quarter_track, uint32_t &bit_position) const override;
  uint8_t readBit(int quarter_track, uint32_t bit_position) const override;

  bool isWriteProtected() const override;
  std::string getFormatName() const override;

  // ===== WOZ-specific methods =====

  /**
   * Get the disk type from INFO chunk
   * @return 1 = 5.25", 2 = 3.5"
   */
  uint8_t getDiskType() const;

  /**
   * Get disk type as a human-readable string
   * @return "5.25\"" or "3.5\""
   */
  std::string getDiskTypeString() const;

  /**
   * Get the optimal bit timing from INFO chunk
   * @return Bit timing in 125ns units (default 32 = 4us)
   */
  uint8_t getOptimalBitTiming() const;

  /**
   * Get the creator software name from INFO chunk
   * @return Creator string (up to 32 chars)
   */
  std::string getCreator() const;

  /**
   * Check if tracks are synchronized
   * @return true if synchronized
   */
  bool isSynchronized() const;

  /**
   * Check if fake bits have been cleaned
   * @return true if cleaned
   */
  bool isCleaned() const;

  /**
   * Get boot sector format (WOZ2)
   * @return 0=unknown, 1=16-sector, 2=13-sector, 3=both
   */
  uint8_t getBootSectorFormat() const;

  /**
   * Get boot sector format as a human-readable string
   * @return Format description
   */
  std::string getBootSectorFormatString() const;

  /**
   * Get number of disk sides (WOZ2)
   * @return 1 or 2
   */
  uint8_t getDiskSides() const;

  /**
   * Get required RAM in KB (WOZ2)
   * @return RAM requirement, or 0 if not specified
   */
  uint16_t getRequiredRAM() const;

private:
  // WOZ file signature constants
  static constexpr uint32_t WOZ1_SIGNATURE = 0x315A4F57; // "WOZ1"
  static constexpr uint32_t WOZ2_SIGNATURE = 0x325A4F57; // "WOZ2"
  static constexpr uint32_t INFO_CHUNK_ID = 0x4F464E49;  // "INFO"
  static constexpr uint32_t TMAP_CHUNK_ID = 0x50414D54;  // "TMAP"
  static constexpr uint32_t TRKS_CHUNK_ID = 0x534B5254;  // "TRKS"

  // Quarter-track mapping
  static constexpr int QUARTER_TRACK_COUNT = 160;
  static constexpr uint8_t NO_TRACK = 0xFF;

  // WOZ2 track storage
  static constexpr size_t WOZ2_TRACK_BLOCK_SIZE = 512;
  static constexpr size_t WOZ2_BITS_BLOCK_SIZE = 512;

  // WOZ1 track storage
  static constexpr size_t WOZ1_TRACK_SIZE = 6656; // Max nibbles per track
  static constexpr size_t WOZ1_TRK_ENTRY_SIZE = 6656 + 2 + 2 + 2 + 2; // data + bytes_used + bit_count + splice_point + splice_nibble + splice_bit_count

#pragma pack(push, 1)
  /**
   * WOZ file header (12 bytes)
   */
  struct WozHeader
  {
    uint32_t signature;     // "WOZ1" or "WOZ2"
    uint8_t high_bits;      // 0xFF
    uint8_t lfcrlf[3];      // 0x0A 0x0D 0x0A
    uint32_t crc32;         // CRC32 of all data after this field
  };

  /**
   * Chunk header (8 bytes)
   */
  struct ChunkHeader
  {
    uint32_t chunk_id;      // 4-character chunk identifier
    uint32_t size;          // Size of chunk data (not including header)
  };

  /**
   * INFO chunk data (60 bytes in WOZ2)
   */
  struct InfoChunk
  {
    uint8_t version;            // INFO chunk version (1 or 2)
    uint8_t disk_type;          // 1 = 5.25", 2 = 3.5"
    uint8_t write_protected;    // 1 = write protected
    uint8_t synchronized;       // 1 = tracks are synchronized
    uint8_t cleaned;            // 1 = MC3470 fake bits removed
    char creator[32];           // Creator software name
    // WOZ 2.0 additional fields
    uint8_t disk_sides;         // 1 or 2
    uint8_t boot_sector_format; // 0=unknown, 1=16-sector, 2=13-sector, 3=both
    uint8_t optimal_bit_timing; // 125ns units (default 32 = 4us)
    uint16_t compatible_hardware; // Bit field of compatible hardware
    uint16_t required_ram;      // Minimum RAM in KB
    uint16_t largest_track;     // Block count of largest track
    // Padding to 60 bytes
    uint8_t reserved[10];
  };

  /**
   * WOZ2 TRKS chunk entry (8 bytes per track)
   */
  struct Woz2TrackEntry
  {
    uint16_t starting_block;  // Starting 512-byte block
    uint16_t block_count;     // Number of 512-byte blocks
    uint32_t bit_count;       // Number of valid bits in track
  };
#pragma pack(pop)

  /**
   * Internal track data storage
   */
  struct TrackData
  {
    std::vector<uint8_t> bits;  // Raw bit data
    uint32_t bit_count = 0;     // Number of valid bits
    bool valid = false;         // Track has data
  };

  // Loaded file info
  std::string filepath_;
  Format format_ = Format::Unknown;
  bool loaded_ = false;

  // INFO chunk data
  InfoChunk info_{};

  // TMAP: quarter-track to track index mapping
  std::array<uint8_t, QUARTER_TRACK_COUNT> tmap_{};

  // Track data storage (indexed by TMAP values, not quarter-track)
  std::vector<TrackData> tracks_;

  // ===== Internal methods =====

  /**
   * Reset all state to unloaded
   */
  void reset();

  /**
   * Parse INFO chunk
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseInfoChunk(const uint8_t *data, uint32_t size);

  /**
   * Parse TMAP chunk
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseTmapChunk(const uint8_t *data, uint32_t size);

  /**
   * Parse TRKS chunk for WOZ1 format
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseTrksChunkWoz1(const uint8_t *data, uint32_t size);

  /**
   * Parse TRKS chunk for WOZ2 format
   * @param data Full file data
   * @param trks_data TRKS chunk data
   * @param trks_size TRKS chunk size
   * @return true on success
   */
  bool parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                          const uint8_t *trks_data, uint32_t trks_size);

  /**
   * Get track data for a quarter-track position
   * @param quarter_track Quarter-track number (0-159)
   * @return Pointer to track data, or nullptr if no data
   */
  const TrackData *getTrackData(int quarter_track) const;
};
