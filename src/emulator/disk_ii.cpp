#include "emulator/disk_ii.hpp"
#include <fstream>
#include <cstring>
#include <iostream>

DiskII::DiskII(int slot)
  : slot_(slot)
  , base_address_(0xC080 + slot * 16)
{
}

AddressRange DiskII::getAddressRange() const
{
  return { base_address_, static_cast<uint16_t>(base_address_ + 15) };
}

std::string DiskII::getName() const
{
  return "Disk II (Slot " + std::to_string(slot_) + ")";
}

uint8_t DiskII::read(uint16_t address)
{
  uint8_t offset = address - base_address_;
  return handleSoftSwitch(offset, false, 0);
}

void DiskII::write(uint16_t address, uint8_t value)
{
  uint8_t offset = address - base_address_;
  handleSoftSwitch(offset, true, value);
}

uint8_t DiskII::handleSoftSwitch(uint8_t offset, bool isWrite, uint8_t value)
{
  switch (offset)
  {
    case PHASE0_OFF:
      setPhase(0, false);
      break;
    case PHASE0_ON:
      setPhase(0, true);
      break;
    case PHASE1_OFF:
      setPhase(1, false);
      break;
    case PHASE1_ON:
      setPhase(1, true);
      break;
    case PHASE2_OFF:
      setPhase(2, false);
      break;
    case PHASE2_ON:
      setPhase(2, true);
      break;
    case PHASE3_OFF:
      setPhase(3, false);
      break;
    case PHASE3_ON:
      setPhase(3, true);
      break;

    case MOTOR_OFF:
      // Motor doesn't turn off immediately - there's a 1 second delay
      if (motor_on_ && !motor_off_pending_)
      {
        motor_off_pending_ = true;
        motor_off_cycle_ = last_cycle_ + MOTOR_OFF_DELAY_CYCLES;
      }
      break;

    case MOTOR_ON:
      motor_on_ = true;
      motor_off_pending_ = false;
      // Ensure track is loaded when motor starts
      loadCurrentTrack();
      // Initialize read timing to current cycle
      last_read_cycle_ = last_cycle_;
      break;

    case DRIVE1:
      if (selected_drive_ != 0)
      {
        flushTrack();
        selected_drive_ = 0;
        loadCurrentTrack();
      }
      break;

    case DRIVE2:
      if (selected_drive_ != 1)
      {
        flushTrack();
        selected_drive_ = 1;
        loadCurrentTrack();
      }
      break;

    case Q6L:
      q6_ = false;
      if (!q7_)
      {
        // Q6L + Q7L = Read mode, return data latch
        return readDataLatch();
      }
      break;

    case Q6H:
      q6_ = true;
      if (!q7_)
      {
        // Q6H + Q7L = Sense write protect
        // Return high bit set if write protected
        const Drive& drive = currentDrive();
        if (drive.disk && drive.write_protected)
        {
          return 0x80;
        }
        return 0x00;
      }
      else if (isWrite)
      {
        // Q6H + Q7H = Write mode, load data latch
        writeDataLatch(value);
      }
      break;

    case Q7L:
      q7_ = false;
      if (!q6_)
      {
        // Q6L + Q7L = Read mode, return data latch
        return readDataLatch();
      }
      break;

    case Q7H:
      q7_ = true;
      break;
  }

  // For most accesses, return the data latch
  return data_latch_;
}

void DiskII::setPhase(int phase, bool on)
{
  if (phase < 0 || phase > 3) return;

  if (on)
  {
    phase_states_ |= (1 << phase);
  }
  else
  {
    phase_states_ &= ~(1 << phase);
  }

  moveHead();
}

void DiskII::moveHead()
{
  // The Disk II stepper motor has 4 phases (0-3).
  // The head moves toward the nearest active phase.
  //
  // IMPORTANT: We track a "virtual" half-track position that represents
  // where the motor THINKS it is, even if the head is clamped at track 0 or 34.
  // This is necessary because the boot ROM recalibrates by stepping outward
  // more times than there are tracks, relying on the physical stop at track 0.
  // If we use clamped position to determine current phase, the stepper logic
  // breaks when stuck at the stops.
  //
  // The stepper sequence for moving inward (increasing track):
  //   Phase 0 -> Phase 1 -> Phase 2 -> Phase 3 -> Phase 0 ...
  // For moving outward (decreasing track):
  //   Phase 0 -> Phase 3 -> Phase 2 -> Phase 1 -> Phase 0 ...
  
  static int move_count = 0;
  static int virtual_half_track = 0;  // Motor's logical position (not clamped)
  
  if (phase_states_ == 0) return;  // No phases active, no movement
  
  // Use virtual position for phase calculation
  int currentPhase = virtual_half_track & 3;
  
  // Check adjacent phases to determine movement direction
  // Phase (current+1)%4 is "inward" (toward higher tracks)
  // Phase (current+3)%4 is "outward" (toward lower tracks)
  int nextPhaseIn = (currentPhase + 1) & 3;
  int nextPhaseOut = (currentPhase + 3) & 3;  // Same as (currentPhase - 1) & 3
  
  int delta = 0;
  
  // Check if the next phase inward is active
  if (phase_states_ & (1 << nextPhaseIn))
  {
    delta = 1;  // Move inward (toward higher tracks)
  }
  // Check if the next phase outward is active
  else if (phase_states_ & (1 << nextPhaseOut))
  {
    delta = -1;  // Move outward (toward lower tracks)
  }
  // If current phase is active but neither adjacent phase, don't move
  // If a phase 2 steps away is active, don't move (invalid/unstable)
  
  if (delta == 0) return;
  
  // Update virtual position (always moves, even past physical limits)
  virtual_half_track += delta;
  
  // Move actual half-track
  int newHalfTrack = current_half_track_ + delta;

  // Clamp to valid range (0-69 for 35 tracks)
  if (newHalfTrack < 0) newHalfTrack = 0;
  if (newHalfTrack > 69) newHalfTrack = 69;

  move_count++;
  if (move_count <= 20) {
    std::cerr << "moveHead #" << move_count << ": half_track " << current_half_track_ 
              << " -> " << newHalfTrack << " (delta=" << delta << ", phases=0x" 
              << std::hex << (int)phase_states_ << std::dec << ")" << std::endl;
  }

  // If track changed, load new track data
  if (newHalfTrack / 2 != current_half_track_ / 2)
  {
    flushTrack();
    current_half_track_ = newHalfTrack;
    loadCurrentTrack();
  }
  else
  {
    current_half_track_ = newHalfTrack;
  }
}

void DiskII::loadCurrentTrack()
{
  Drive& drive = currentDrive();

  int track = current_half_track_ / 2;

  if (!drive.disk || !drive.disk->isLoaded())
  {
    drive.nibble_track.clear();
    drive.current_track = track;
    return;
  }

  // Only reload if track changed
  if (drive.current_track == track && !drive.nibble_track.empty())
  {
    return;
  }

  drive.nibble_track = drive.disk->getNibblizedTrack(track);
  drive.current_track = track;
  drive.track_dirty = false;

  // Reset position to start of track if track changed
  if (drive.byte_position >= drive.nibble_track.size())
  {
    drive.byte_position = 0;
  }
  
  // Prime the data latch with the current byte
  if (!drive.nibble_track.empty())
  {
    data_latch_ = drive.nibble_track[drive.byte_position];
    last_read_cycle_ = (last_cycle_ > 32) ? last_cycle_ - 32 : 0;
  }
  
  // Log track load for track 0
  if (track == 0) {
    std::cerr << "\n*** DISK II: Loaded nibble_track for track 0 ***\n";
    std::cerr << "  Track size: " << drive.nibble_track.size() << " bytes\n";
    std::cerr << "  First 16 bytes: ";
    for (size_t i = 0; i < 16 && i < drive.nibble_track.size(); i++) {
      std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)drive.nibble_track[i] << " ";
    }
    std::cerr << std::dec << "\n";
    std::cerr << "  Data latch primed with: 0x" << std::hex << (int)data_latch_ << std::dec << "\n";
    std::cerr << "*** END DISK II LOAD ***\n\n";
  }
}

void DiskII::flushTrack()
{
  Drive& drive = currentDrive();

  if (!drive.track_dirty || !drive.disk || drive.nibble_track.empty())
  {
    return;
  }

  // Decode the modified track back to sector data
  drive.disk->decodeTrack(drive.current_track, drive.nibble_track);
  drive.track_dirty = false;
}

void DiskII::update(uint64_t cpuCycles)
{
  // Handle motor shutoff
  if (motor_off_pending_ && cpuCycles >= motor_off_cycle_)
  {
    motor_on_ = false;
    motor_off_pending_ = false;
  }

  // Update cycle count for timing calculations in readDataLatch
  last_cycle_ = cpuCycles;
}

void DiskII::advancePosition(uint64_t cycles)
{
  Drive& drive = currentDrive();

  if (drive.nibble_track.empty()) return;

  // Calculate bytes elapsed
  // At 1.023 MHz, 4 cycles per bit = 32 cycles per byte
  size_t bytes = cycles / 32;

  if (bytes > 0)
  {
    drive.byte_position = (drive.byte_position + bytes) % drive.nibble_track.size();
  }
}

uint8_t DiskII::readDataLatch()
{
  static bool verified = false;
  static int read_count = 0;
  static int call_count = 0;
  
  call_count++;
  
  if (!motor_on_) return data_latch_;

  Drive& drive = currentDrive();

  if (drive.nibble_track.empty())
  {
    // No disk - return 0xFF or random data
    return 0xFF;
  }
  
  // One-time verification of sector 0 data checksum
  if (!verified && drive.nibble_track.size() > 420) {
    verified = true;
    // Find data field at pos 77 (after D5 AA AD at 74-76)
    // Verify checksum boot-ROM style
    uint8_t prev = 0;
    bool valid = true;
    for (int i = 0; i < 343 && valid; i++) {
      uint8_t encoded = drive.nibble_track[77 + i];
      // Decode through nibble table
      uint8_t decoded = 0xFF;
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
      for (int j = 0; j < 64; j++) {
        if (NIBBLE_ENCODE[j] == encoded) { decoded = j; break; }
      }
      if (decoded == 0xFF) { valid = false; break; }
      uint8_t val = decoded ^ prev;
      prev = decoded;
      if (i == 342) {
        std::cerr << "SECTOR 0 CHECKSUM: val=0x" << std::hex << (int)val 
                  << (val == 0 ? " PASS" : " FAIL") << std::dec << std::endl;
      }
    }
  }
  
  // Log ALL reads to see the actual byte sequence
  read_count++;
  static int log_count = 0;
  
  // Log first 50 reads after recal (read #2600+)
  if (read_count >= 2620 && read_count <= 2700) {
    log_count++;
    std::cerr << "RD[" << read_count << "] pos=" << drive.byte_position 
              << " track=0x" << std::hex << (int)drive.nibble_track[drive.byte_position]
              << " cyc=" << std::dec << last_cycle_ 
              << " last=" << last_read_cycle_ 
              << " valid=" << data_valid_ << std::endl;
  }
  
  // Check if enough time has passed for a new byte to be available
  // At 1.023 MHz, 4 cycles per bit = 32 cycles per byte
  constexpr uint64_t CYCLES_PER_BYTE = 32;
  
  static int timing_debug = 0;
  static size_t last_pos = 0;
  timing_debug++;
  
  // Show when we find D5 (address prologue marker)
  if (drive.nibble_track[drive.byte_position] == 0xD5) {
    std::cerr << "FOUND D5 at pos=" << drive.byte_position << " (read #" << timing_debug << ")" << std::endl;
  }
  
  // The Disk II controller uses a sequencer that shifts bits from the disk.
  // When 8 bits have been shifted in and the high bit is 1 (valid GCR nibble),
  // the data is latched and ready. The boot ROM waits with "LDA $C08C,X / BPL loop".
  //
  // Critically: The hardware sets the data latch to the disk byte (which always
  // has bit 7 set for valid GCR nibbles), but there's a "valid" flag that gets
  // cleared on read. So:
  // - First read after new byte: returns byte with bit 7 set (e.g., $D5)
  // - Subsequent reads before next byte: returns same byte with bit 7 CLEAR (e.g., $55)
  
  constexpr uint64_t BYTE_CYCLES = 32;
  
  // Debug: track how often we set data_valid
  static int set_valid_count = 0;
  static int check_count = 0;
  check_count++;
  
  static int entered_timing = 0;
  
  if (last_cycle_ >= last_read_cycle_ + BYTE_CYCLES)
  {
    entered_timing++;
    // Calculate how many bytes have rotated past since last read
    uint64_t elapsed = last_cycle_ - last_read_cycle_;
    size_t bytes_elapsed = elapsed / BYTE_CYCLES;
    
    if (entered_timing <= 10) {
      std::cerr << "TIMING[" << entered_timing << "] elapsed=" << elapsed 
                << " bytes=" << bytes_elapsed << " track_empty=" << drive.nibble_track.empty() 
                << std::endl;
    }
    
    if (bytes_elapsed > 0 && !drive.nibble_track.empty())
    {
      drive.byte_position = (drive.byte_position + bytes_elapsed) % drive.nibble_track.size();
      last_read_cycle_ = last_cycle_;
      data_valid_ = true;  // New byte is ready
      set_valid_count++;
      // Show after cycle 1585000 (after recal which is at 1584965) 
      if (set_valid_count <= 20 || (last_cycle_ > 1585000 && set_valid_count <= 120)) {
        std::cerr << "SET_VALID[" << set_valid_count << "] pos=" << drive.byte_position 
                  << " cyc=" << last_cycle_ << std::endl;
      }
    }
    
    // Load new byte into data latch
    data_latch_ = drive.nibble_track[drive.byte_position];
  }

  // Return byte - if valid, return as-is (bit 7 set); if not valid, clear bit 7
  uint8_t result;
  static int total_reads = 0;
  total_reads++;
  
  if (data_valid_)
  {
    data_valid_ = false;  // Clear valid flag after read
    result = data_latch_;   // Return with bit 7 intact
    
    // Debug valid returns
    static int valid_cnt = 0;
    valid_cnt++;
    
    // Track sector data reads - after finding D5 AA AD at specific positions
    // Sector 0 data field starts at position 77 (D5 AA AD at 74-76)
    static bool in_sector0_data = false;
    static int sector0_byte_count = 0;
    
    if (drive.byte_position >= 74 && drive.byte_position <= 76 && result == 0xD5) {
      // Found D5 near sector 0 data field
      in_sector0_data = false;  // Reset, will be set when we see AD
    }
    if (drive.byte_position == 76 && result == 0xAD) {
      in_sector0_data = true;
      sector0_byte_count = 0;
      std::cerr << "SECTOR0_DATA_START at pos=76" << std::endl;
    }
    if (in_sector0_data && drive.byte_position >= 77 && drive.byte_position < 77+343) {
      sector0_byte_count++;
      if (sector0_byte_count <= 5 || sector0_byte_count >= 341) {
        std::cerr << "SECTOR0_DATA[" << sector0_byte_count << "] pos=" << drive.byte_position 
                  << " nibble=0x" << std::hex << (int)result << std::dec << std::endl;
      }
      if (sector0_byte_count == 343) {
        in_sector0_data = false;
      }
    }
    
    if (result == 0xD5) {
      std::cerr << "VALID_D5[" << valid_cnt << "] pos=" << drive.byte_position 
                << " cyc=" << last_cycle_ << std::endl;
    }
  }
  else
  {
    result = data_latch_ & 0x7F;  // Not ready - clear bit 7
  }
  
  return result;
}

void DiskII::writeDataLatch(uint8_t value)
{
  if (!motor_on_ || !q7_) return;  // Must be in write mode with motor on

  Drive& drive = currentDrive();

  if (drive.nibble_track.empty() || drive.write_protected)
  {
    return;
  }

  // Write nibble to track
  drive.nibble_track[drive.byte_position] = value;
  drive.track_dirty = true;

  // Advance to next byte
  drive.byte_position = (drive.byte_position + 1) % drive.nibble_track.size();
}

bool DiskII::insertDisk(int drive, const std::string& filepath)
{
  if (drive < 0 || drive > 1) return false;

  Drive& drv = drives_[drive];

  // Create new disk image
  drv.disk = std::make_unique<DiskImage>();

  if (!drv.disk->load(filepath))
  {
    drv.disk.reset();
    return false;
  }

  drv.write_protected = true;  // Default to write protected
  drv.nibble_track.clear();
  drv.byte_position = 0;
  drv.track_dirty = false;

  // Load current track if this is the selected drive
  if (drive == selected_drive_)
  {
    loadCurrentTrack();
  }

  return true;
}

void DiskII::ejectDisk(int drive)
{
  if (drive < 0 || drive > 1) return;

  Drive& drv = drives_[drive];

  // Flush any pending writes
  if (drv.track_dirty && drv.disk)
  {
    drv.disk->decodeTrack(drv.current_track, drv.nibble_track);
    drv.disk->save();
  }

  drv.disk.reset();
  drv.nibble_track.clear();
  drv.byte_position = 0;
  drv.track_dirty = false;
}

bool DiskII::isDiskInserted(int drive) const
{
  if (drive < 0 || drive > 1) return false;
  return drives_[drive].disk != nullptr && drives_[drive].disk->isLoaded();
}

bool DiskII::isWriteProtected(int drive) const
{
  if (drive < 0 || drive > 1) return true;
  return drives_[drive].write_protected;
}

void DiskII::setWriteProtected(int drive, bool protect)
{
  if (drive < 0 || drive > 1) return;
  drives_[drive].write_protected = protect;
}

const DiskImage* DiskII::getDiskImage(int drive) const
{
  if (drive < 0 || drive > 1) return nullptr;
  return drives_[drive].disk.get();
}

void DiskII::saveState(std::ostream& out) const
{
  // Save controller state
  out.write(reinterpret_cast<const char*>(&selected_drive_), sizeof(selected_drive_));
  out.write(reinterpret_cast<const char*>(&motor_on_), sizeof(motor_on_));
  out.write(reinterpret_cast<const char*>(&phase_states_), sizeof(phase_states_));
  out.write(reinterpret_cast<const char*>(&current_half_track_), sizeof(current_half_track_));
  out.write(reinterpret_cast<const char*>(&q6_), sizeof(q6_));
  out.write(reinterpret_cast<const char*>(&q7_), sizeof(q7_));
  out.write(reinterpret_cast<const char*>(&data_latch_), sizeof(data_latch_));
  out.write(reinterpret_cast<const char*>(&motor_off_pending_), sizeof(motor_off_pending_));
  out.write(reinterpret_cast<const char*>(&last_read_cycle_), sizeof(last_read_cycle_));

  // Save drive state for each drive
  for (int d = 0; d < 2; d++)
  {
    const Drive& drv = drives_[d];

    bool hasDisk = (drv.disk != nullptr && drv.disk->isLoaded());
    out.write(reinterpret_cast<const char*>(&hasDisk), sizeof(hasDisk));

    if (hasDisk)
    {
      // Save disk file path
      const std::string& path = drv.disk->getFilePath();
      size_t pathLen = path.length();
      out.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
      out.write(path.c_str(), pathLen);

      // Save drive position
      out.write(reinterpret_cast<const char*>(&drv.byte_position), sizeof(drv.byte_position));
      out.write(reinterpret_cast<const char*>(&drv.write_protected), sizeof(drv.write_protected));
    }
  }
}

void DiskII::loadState(std::istream& in)
{
  // Load controller state
  in.read(reinterpret_cast<char*>(&selected_drive_), sizeof(selected_drive_));
  in.read(reinterpret_cast<char*>(&motor_on_), sizeof(motor_on_));
  in.read(reinterpret_cast<char*>(&phase_states_), sizeof(phase_states_));
  in.read(reinterpret_cast<char*>(&current_half_track_), sizeof(current_half_track_));
  in.read(reinterpret_cast<char*>(&q6_), sizeof(q6_));
  in.read(reinterpret_cast<char*>(&q7_), sizeof(q7_));
  in.read(reinterpret_cast<char*>(&data_latch_), sizeof(data_latch_));
  in.read(reinterpret_cast<char*>(&motor_off_pending_), sizeof(motor_off_pending_));
  in.read(reinterpret_cast<char*>(&last_read_cycle_), sizeof(last_read_cycle_));

  // Load drive state for each drive
  for (int d = 0; d < 2; d++)
  {
    Drive& drv = drives_[d];

    bool hasDisk;
    in.read(reinterpret_cast<char*>(&hasDisk), sizeof(hasDisk));

    if (hasDisk)
    {
      // Load disk file path
      size_t pathLen;
      in.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));

      std::string path(pathLen, '\0');
      in.read(&path[0], pathLen);

      // Reload the disk
      insertDisk(d, path);

      // Restore drive position
      in.read(reinterpret_cast<char*>(&drv.byte_position), sizeof(drv.byte_position));
      in.read(reinterpret_cast<char*>(&drv.write_protected), sizeof(drv.write_protected));
    }
    else
    {
      drv.disk.reset();
      drv.nibble_track.clear();
    }
  }

  // Reload current track
  loadCurrentTrack();
}
