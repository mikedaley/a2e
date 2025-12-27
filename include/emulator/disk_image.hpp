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
 * The disk image manages head positioning internally based on stepper
 * motor phase changes. The controller only knows about phases (0-3),
 * not tracks. The disk image translates phase sequences into head
 * movement using 4-phase stepper motor physics.
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
    WOZ2,
    DSK,  // Raw sector format (140KB, 35 tracks × 16 sectors × 256 bytes)
    DO,   // DOS-order DSK (same as DSK)
    PO    // ProDOS-order DSK
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

  // ===== Head Positioning =====

  /**
   * Notify the disk of a phase magnet state change
   * The disk image tracks phase states and moves the head accordingly
   * using 4-phase stepper motor physics.
   *
   * @param phase Phase number (0-3)
   * @param on true if phase is being activated, false if deactivated
   */
  virtual void setPhase(int phase, bool on) = 0;

  /**
   * Get the current quarter-track position (0-159)
   * This is for display/debugging purposes only.
   * @return Current quarter-track position
   */
  virtual int getQuarterTrack() const = 0;

  /**
   * Get the current track position (0-39)
   * This is for display/debugging purposes only.
   * @return Current track number (quarter_track / 4)
   */
  virtual int getTrack() const = 0;

  // ===== Geometry =====

  /**
   * Get the number of tracks on the disk
   * Standard 5.25" disks have 35 tracks (0-34)
   * @return Number of tracks
   */
  virtual int getTrackCount() const = 0;

  /**
   * Check if current head position has data
   * @return true if the current position has data
   */
  virtual bool hasData() const = 0;

  // ===== Data Access =====

  /**
   * Advance the bit position based on elapsed CPU cycles
   * This simulates the disk rotating while the motor is on.
   *
   * @param elapsed_cycles Number of CPU cycles since last call
   */
  virtual void advanceBitPosition(uint64_t elapsed_cycles) = 0;

  /**
   * Read a nibble from the disk at the current position
   * This simulates the disk read head reading data.
   * The bit position advances as bits are read until a complete
   * nibble (byte with high bit set) is assembled.
   *
   * @return The nibble read (high bit set for valid data)
   */
  virtual uint8_t readNibble() = 0;

  // ===== Write Operations =====

  /**
   * Write a nibble to the disk at the current position
   * This simulates the disk write head writing data.
   * The bit position advances as bits are written.
   * Changes are immediately persisted to the disk file.
   *
   * @param nibble The nibble to write (should have high bit set for valid GCR data)
   */
  virtual void writeNibble(uint8_t nibble) = 0;

  /**
   * Save the disk image back to the file
   * @return true on success, false on failure
   */
  virtual bool save() = 0;

  /**
   * Save the disk image to a new file
   * @param filepath Path to save to
   * @return true on success, false on failure
   */
  virtual bool saveAs(const std::string &filepath) = 0;

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
