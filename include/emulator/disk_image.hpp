#pragma once

#include <cstdint>
#include <string>

/**
 * DiskImage - Abstract base class for disk image formats
 *
 * This interface defines the contract for disk image implementations.
 * The disk controller uses this interface to access disk data without
 * needing to know the specific format details.
 *
 * Supported coordinate systems:
 * - Half-tracks (0-69): Standard Disk II positioning
 * - Quarter-tracks (0-139): Fine positioning for copy-protected disks
 *
 * Data access is nibble-based. The controller reads nibbles sequentially
 * as the virtual disk spins. Each track contains a stream of nibbles
 * that encode the sector data using GCR (Group Coded Recording).
 */
class DiskImage
{
public:
  /**
   * Disk image format types
   */
  enum class Format
  {
    Unknown,
    WOZ1,
    WOZ2
  };

  virtual ~DiskImage() = default;

  // Non-copyable
  DiskImage(const DiskImage &) = delete;
  DiskImage &operator=(const DiskImage &) = delete;

  // Movable
  DiskImage(DiskImage &&) = default;
  DiskImage &operator=(DiskImage &&) = default;

  // ===== Loading =====

  /**
   * Load a disk image from a file
   * @param filepath Path to the disk image file
   * @return true on success, false on failure
   */
  virtual bool load(const std::string &filepath) = 0;

  /**
   * Check if a disk image is currently loaded
   * @return true if a valid disk image is loaded
   */
  virtual bool isLoaded() const = 0;

  /**
   * Get the filepath of the currently loaded image
   * @return filepath string, or empty if not loaded
   */
  virtual const std::string &getFilepath() const = 0;

  /**
   * Get the format of the loaded disk image
   * @return Format enum value
   */
  virtual Format getFormat() const = 0;

  // ===== Geometry =====

  /**
   * Get the number of tracks on the disk
   * Standard 5.25" disks have 35 tracks (0-34)
   * @return Number of tracks
   */
  virtual int getTrackCount() const = 0;

  /**
   * Check if a quarter-track position contains data
   * WOZ format supports quarter-track positioning for copy protection
   * @param quarter_track Quarter-track number (0-139)
   * @return true if the quarter-track has data
   */
  virtual bool hasQuarterTrack(int quarter_track) const = 0;

  /**
   * Get the bit count for a track (WOZ format)
   * This represents the length of valid data on the track
   * @param quarter_track Quarter-track number (0-139)
   * @return Number of bits, or 0 if track has no data
   */
  virtual uint32_t getTrackBitCount(int quarter_track) const = 0;

  // ===== Data Access =====

  /**
   * Read a nibble from the disk at the current bit position
   * This simulates the disk rotating and the head reading data.
   * The bit position advances automatically with each read.
   *
   * @param quarter_track Quarter-track position (0-139)
   * @param bit_position Current bit position (modified on return)
   * @return The nibble read (high bit set for valid data)
   */
  virtual uint8_t readNibble(int quarter_track, uint32_t &bit_position) const = 0;

  /**
   * Read a raw bit from the disk
   * Lower-level access for precise timing emulation
   *
   * @param quarter_track Quarter-track position (0-139)
   * @param bit_position Bit position within the track
   * @return 0 or 1
   */
  virtual uint8_t readBit(int quarter_track, uint32_t bit_position) const = 0;

  // ===== Status =====

  /**
   * Check if the disk is write-protected
   * @return true if write-protected
   */
  virtual bool isWriteProtected() const = 0;

  /**
   * Get a human-readable format name
   * @return Format name string (e.g., "WOZ 2.0")
   */
  virtual std::string getFormatName() const = 0;

protected:
  DiskImage() = default;
};
