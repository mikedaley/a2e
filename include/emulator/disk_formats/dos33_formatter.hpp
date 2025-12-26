#pragma once

#include <array>
#include <cstdint>
#include <vector>

/**
 * DOS33DiskFormatter - Creates DOS 3.3 formatted disk structure
 *
 * DOS 3.3 disk layout for 5.25" floppy:
 * - 35 tracks (0-34), 16 sectors per track (0-15)
 * - 256 bytes per sector
 * - Track 0: Boot sector (sector 0) + DOS image
 * - Tracks 1-2: DOS image continued
 * - Track 17: VTOC (sector 0) and Catalog (sectors 15-1)
 * - Remaining tracks: Data tracks available for files
 *
 * Physical sector interleaving is used to improve performance:
 * Logical:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 * Physical: 0 13 11  9  7  5  3  1 14 12 10  8  6  4  2 15
 */
class DOS33DiskFormatter
{
public:
  // Disk geometry constants
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int VTOC_TRACK = 17;
  static constexpr int VTOC_SECTOR = 0;
  static constexpr int FIRST_CATALOG_SECTOR = 15;

  // Default volume number
  static constexpr uint8_t DEFAULT_VOLUME = 254;

  /**
   * Constructor
   * @param volume_number Volume number (1-254, default 254)
   */
  explicit DOS33DiskFormatter(uint8_t volume_number = DEFAULT_VOLUME);

  /**
   * Generate disk as nibblized track data suitable for WOZ format
   * Each track is a vector of bytes containing the bit stream
   * @return Vector of 35 tracks, each containing bit data
   */
  std::vector<std::vector<uint8_t>> generateNibblizedTracks();

  /**
   * Get the bit count for each track
   * @return Vector of 35 bit counts
   */
  std::vector<uint32_t> getTrackBitCounts() const;

  /**
   * Get the volume number
   */
  uint8_t getVolumeNumber() const { return volume_number_; }

private:
  uint8_t volume_number_;

  // Sector data storage during generation
  std::array<std::array<std::array<uint8_t, BYTES_PER_SECTOR>,
                        SECTORS_PER_TRACK>,
             TRACKS>
      disk_data_;

  // Track bit counts (set after generation)
  mutable std::vector<uint32_t> track_bit_counts_;

  /**
   * DOS 3.3 logical to physical sector interleaving
   * This maps logical sector numbers to physical positions on the track
   */
  static constexpr std::array<int, 16> LOGICAL_TO_PHYSICAL = {
      0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};

  /**
   * Physical to logical sector mapping (reverse of above)
   */
  static constexpr std::array<int, 16> PHYSICAL_TO_LOGICAL = {
      0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};

  /**
   * Initialize all sectors to zero
   */
  void initializeDiskData();

  /**
   * Build the VTOC (Volume Table of Contents) sector
   * Track 17, Sector 0
   */
  void buildVTOC();

  /**
   * Build the catalog sectors
   * Track 17, Sectors 15 down to 1 (linked list)
   */
  void buildCatalog();

  /**
   * Build a single catalog sector
   * @param sector_num Sector number (15 down to 1)
   * @param next_sector Next catalog sector (or 0 if last)
   */
  void buildCatalogSector(int sector_num, int next_sector);

  /**
   * Build the track free bitmap for VTOC
   * Marks DOS tracks (0-2, 17) as used, others as free
   * @param bitmap Output array for bitmap data (140 bytes)
   */
  void buildTrackBitmap(uint8_t *bitmap);

  /**
   * Convert nibbles to bit stream
   * @param nibbles Vector of nibble bytes
   * @return Vector of bytes containing packed bits
   */
  std::vector<uint8_t> nibblesToBits(const std::vector<uint8_t> &nibbles);

  /**
   * Build a complete track as nibblized data
   * @param track Track number (0-34)
   * @return Vector of nibbles for the track
   */
  std::vector<uint8_t> buildTrackNibbles(int track);
};
