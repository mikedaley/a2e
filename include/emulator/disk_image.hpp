#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Disk image format types
 */
enum class DiskFormat
{
  DOS33,  // .dsk, .do - DOS 3.3 sector ordering
  PRODOS  // .po - ProDOS sector ordering
};

/**
 * DiskImage - Handles loading, saving, and nibblizing Apple II disk images
 *
 * Supports DSK/DO (DOS 3.3) and PO (ProDOS) disk image formats.
 * Provides nibblization (converting raw sectors to GCR-encoded track data)
 * and denibblization (converting back) for DISK II emulation.
 */
class DiskImage
{
public:
  // Disk geometry constants
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int DISK_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;  // 143,360 bytes
  static constexpr int NIBBLES_PER_TRACK = 6656;  // Standard nibblized track size

  /**
   * Default constructor - creates empty disk image
   */
  DiskImage();

  /**
   * Load a disk image from file
   * @param filepath Path to .dsk, .do, or .po file
   * @return true on success
   */
  bool load(const std::string& filepath);

  /**
   * Save the disk image to file
   * @param filepath Path to save to
   * @return true on success
   */
  bool save(const std::string& filepath);

  /**
   * Save the disk image to its original file
   * @return true on success
   */
  bool save();

  /**
   * Check if a disk image is loaded
   */
  bool isLoaded() const { return loaded_; }

  /**
   * Check if the disk has been modified
   */
  bool isModified() const { return modified_; }

  /**
   * Get the disk format
   */
  DiskFormat getFormat() const { return format_; }

  /**
   * Get the file path
   */
  const std::string& getFilePath() const { return filepath_; }

  /**
   * Get raw sector data (256 bytes)
   * @param track Track number (0-34)
   * @param sector Logical sector number (0-15)
   * @return Pointer to sector data, or nullptr if invalid
   */
  const uint8_t* getSector(int track, int sector) const;

  /**
   * Get mutable sector data
   * @param track Track number (0-34)
   * @param sector Logical sector number (0-15)
   * @return Pointer to sector data, or nullptr if invalid
   */
  uint8_t* getSectorMutable(int track, int sector);

  /**
   * Get a nibblized track (GCR encoded, ready for DISK II emulation)
   * @param track Track number (0-34)
   * @param volume Volume number (default 254)
   * @return Vector of nibblized bytes
   */
  std::vector<uint8_t> getNibblizedTrack(int track, uint8_t volume = 254) const;

  /**
   * Decode a nibblized track back to sector data
   * @param track Track number (0-34)
   * @param nibbleData The nibblized track data
   * @return true if decoding succeeded
   */
  bool decodeTrack(int track, const std::vector<uint8_t>& nibbleData);

  /**
   * Format a blank disk (all sectors zeroed)
   */
  void format();

private:
  bool loaded_ = false;
  bool modified_ = false;
  DiskFormat format_ = DiskFormat::DOS33;
  std::string filepath_;
  std::array<uint8_t, DISK_SIZE> data_;

  // DOS 3.3 logical-to-physical sector mapping
  static constexpr int DOS33_SECTOR_MAP[16] = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
  };

  // ProDOS logical-to-physical sector mapping
  static constexpr int PRODOS_SECTOR_MAP[16] = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
  };

  // 6-and-2 GCR encoding table (6-bit value -> 8-bit disk byte)
  // This table matches what the Disk II boot ROM generates at runtime.
  // The boot ROM accepts nibbles with: high bit set, at least two adjacent 1s,
  // and no two adjacent 0s in bits 1-6. Only 64 values meet these criteria.
  static constexpr uint8_t NIBBLE_ENCODE[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
  };

  // Reverse lookup table (8-bit disk byte -> 6-bit value)
  // Generated at startup, 0xFF = invalid
  static uint8_t nibble_decode_[256];
  static bool decode_table_initialized_;

  /**
   * Initialize the decode lookup table
   */
  static void initializeDecodeTable();

  /**
   * Detect disk format from file extension
   */
  DiskFormat detectFormat(const std::string& filepath) const;

  /**
   * Get physical sector offset in file for a logical sector
   */
  size_t getSectorOffset(int track, int logicalSector) const;

  /**
   * Nibblize a single sector (256 bytes -> ~400 bytes with headers)
   */
  std::vector<uint8_t> nibblizeSector(int track, int sector, uint8_t volume) const;

  /**
   * Encode 256 bytes using 6-and-2 encoding -> 343 bytes (342 data + 1 checksum)
   */
  void encode6and2(const uint8_t* src, uint8_t* dest) const;

  /**
   * Decode 6-and-2 encoded data (343 bytes -> 256 bytes)
   * @return true if checksum valid
   */
  bool decode6and2(const uint8_t* src, uint8_t* dest) const;

  /**
   * Encode a byte using 4-and-4 encoding (1 byte -> 2 bytes)
   */
  void encode4and4(uint8_t value, uint8_t* dest) const;

  /**
   * Decode 4-and-4 encoded value (2 bytes -> 1 byte)
   */
  uint8_t decode4and4(const uint8_t* src) const;
};
