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

  uint8_t old_phases = phase_states_;
  
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
  
  if (phase_states_ == 0) return;  // No phases active, no movement
  
  // Use virtual position for phase calculation (& 3 works for negative numbers in C++)
  int currentPhase = virtual_half_track_ & 3;
  
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
  virtual_half_track_ += delta;
  
  // Move actual half-track
  int newHalfTrack = current_half_track_ + delta;

  // Clamp to valid range (0-69 for 35 tracks)
  if (newHalfTrack < 0) newHalfTrack = 0;
  if (newHalfTrack > 69) newHalfTrack = 69;

  // If half-track changed
  if (newHalfTrack != current_half_track_)
  {
    int oldTrack = current_half_track_ / 2;
    int newTrack = newHalfTrack / 2;
    
    std::cerr << "TRACK: half " << current_half_track_ << " -> " << newHalfTrack
              << " (track " << oldTrack << " -> " << newTrack << ")" << std::endl;
    
    // If whole track changed, load new track data
    if (newTrack != oldTrack)
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
  if (!motor_on_) return data_latch_;

  Drive& drive = currentDrive();

  if (drive.nibble_track.empty())
  {
    // No disk - return 0xFF
    return 0xFF;
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
  
  constexpr uint64_t BYTE_CYCLES = 32;  // 4 cycles per bit * 8 bits
  
  if (last_cycle_ >= last_read_cycle_ + BYTE_CYCLES)
  {
    // Calculate how many bytes have rotated past since last read
    uint64_t elapsed = last_cycle_ - last_read_cycle_;
    size_t bytes_elapsed = elapsed / BYTE_CYCLES;
    
    if (bytes_elapsed > 0)
    {
      drive.byte_position = (drive.byte_position + bytes_elapsed) % drive.nibble_track.size();
      last_read_cycle_ = last_cycle_;
      data_valid_ = true;  // New byte is ready
    }
    
    // Load new byte into data latch
    data_latch_ = drive.nibble_track[drive.byte_position];
  }

  // Return byte - if valid, return as-is (bit 7 set); if not valid, clear bit 7
  if (data_valid_)
  {
    data_valid_ = false;  // Clear valid flag after read
    return data_latch_;   // Return with bit 7 intact
  }
  else
  {
    return data_latch_ & 0x7F;  // Not ready - clear bit 7
  }
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
