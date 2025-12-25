#pragma once

#include <array>
#include <cstdint>

/**
 * access_type - Type of memory access
 */
enum class access_type : uint8_t
{
  NONE = 0,
  READ = 1,
  WRITE = 2
};

/**
 * memory_access_entry - State for a single memory location
 */
struct memory_access_entry
{
  access_type type = access_type::NONE;
  float fade_timer = 0.0f; // Seconds remaining until fade complete
};

/**
 * memory_access_tracker - Tracks memory read/write accesses for visualization
 *
 * Records when each memory location is read or written, and maintains
 * fade timers for visual display. Can be enabled/disabled for performance.
 */
class memory_access_tracker
{
public:
  static constexpr float FADE_DURATION = 1.0f; // 1 second fade

  /**
   * Record a read access at an address
   * @param address Memory address that was read
   */
  void recordRead(uint16_t address);

  /**
   * Record a write access at an address
   * @param address Memory address that was written
   */
  void recordWrite(uint16_t address);

  /**
   * Update fade timers (call once per frame)
   * @param deltaTime Time elapsed since last update in seconds
   */
  void update(float deltaTime);

  /**
   * Get the access entry for an address
   * @param address Memory address
   * @return Reference to the access entry
   */
  const memory_access_entry &getEntry(uint16_t address) const;

  /**
   * Enable or disable tracking
   * When disabled, recordRead/recordWrite do nothing
   * @param enabled True to enable tracking
   */
  void setEnabled(bool enabled);

  /**
   * Check if tracking is enabled
   * @return True if tracking is enabled
   */
  bool isEnabled() const;

  /**
   * Clear all access state (reset to NONE)
   */
  void clear();

private:
  std::array<memory_access_entry, 65536> entries_{};
  bool enabled_ = false;
};
