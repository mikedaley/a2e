#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

/**
 * breakpoint_type - Types of breakpoints supported
 */
enum class breakpoint_type
{
  EXECUTION,  // Break when PC reaches address
  READ,       // Break when address is read
  WRITE       // Break when address is written
};

/**
 * breakpoint - Represents a single breakpoint
 */
struct breakpoint
{
  uint16_t address;
  breakpoint_type type;
  bool enabled = true;
};

/**
 * breakpoint_manager - Manages all breakpoints and watchpoints
 *
 * Provides fast O(1) lookup for breakpoint checks during execution.
 * Supports serialization for persistent storage across sessions.
 */
class breakpoint_manager
{
public:
  breakpoint_manager() = default;

  /**
   * Add a breakpoint at the specified address
   * @param address Memory address
   * @param type Type of breakpoint
   */
  void addBreakpoint(uint16_t address, breakpoint_type type);

  /**
   * Remove a breakpoint at the specified address
   * @param address Memory address
   * @param type Type of breakpoint
   */
  void removeBreakpoint(uint16_t address, breakpoint_type type);

  /**
   * Toggle a breakpoint (add if not exists, remove if exists)
   * @param address Memory address
   * @param type Type of breakpoint
   */
  void toggleBreakpoint(uint16_t address, breakpoint_type type);

  /**
   * Check if a breakpoint exists at the specified address
   * @param address Memory address
   * @param type Type of breakpoint
   * @return true if breakpoint exists
   */
  bool hasBreakpoint(uint16_t address, breakpoint_type type) const;

  /**
   * Enable or disable a breakpoint
   * @param address Memory address
   * @param type Type of breakpoint
   * @param enabled true to enable, false to disable
   */
  void setEnabled(uint16_t address, breakpoint_type type, bool enabled);

  /**
   * Check if execution should break at the given PC
   * @param pc Program counter value
   * @return true if should break
   */
  bool checkExecution(uint16_t pc) const;

  /**
   * Check if read should break at the given address
   * @param address Memory address being read
   * @return true if should break
   */
  bool checkRead(uint16_t address) const;

  /**
   * Check if write should break at the given address
   * @param address Memory address being written
   * @return true if should break
   */
  bool checkWrite(uint16_t address) const;

  /**
   * Get all breakpoints
   * @return Vector of all breakpoints
   */
  const std::vector<breakpoint>& getBreakpoints() const { return breakpoints_; }

  /**
   * Serialize breakpoints to string for persistence
   * Format: "ADDR:TYPE:ENABLED;ADDR:TYPE:ENABLED;..."
   * @return Serialized string
   */
  std::string serialize() const;

  /**
   * Deserialize breakpoints from string
   * @param data Serialized breakpoint data
   */
  void deserialize(const std::string& data);

  /**
   * Clear all breakpoints
   */
  void clear();

private:
  std::vector<breakpoint> breakpoints_;

  // Fast lookup maps (address -> indices in breakpoints_ vector)
  std::unordered_map<uint16_t, std::vector<size_t>> execution_map_;
  std::unordered_map<uint16_t, std::vector<size_t>> read_map_;
  std::unordered_map<uint16_t, std::vector<size_t>> write_map_;

  /**
   * Rebuild lookup maps after modifications
   */
  void rebuildMaps();

  /**
   * Find breakpoint index in vector
   * @return Index or -1 if not found
   */
  size_t findBreakpoint(uint16_t address, breakpoint_type type) const;
};
