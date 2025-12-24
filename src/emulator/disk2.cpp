#include "emulator/disk2.hpp"
#include <iostream>
#include <iomanip>

// Magnet-to-position lookup table from apple2ts
// Maps the 4-bit phase bitmask to a position (0-7), or -1 if invalid
// Index is the bitmask of currently-on phases (bits 0-3 = phases 0-3)
static const int MAGNET_TO_POSITION[16] = {
  -1,   0,   2,   1,   4,  -1,   3,  -1,
   6,   7,  -1,  -1,   5,  -1,  -1,  -1
};

// Position-to-direction lookup table from apple2ts
// Maps [last_position][new_position] to direction of movement (-3 to +3)
// Each position represents 1/8 of a track (quarter-track resolution)
static const int POSITION_TO_DIRECTION[8][8] = {
  {  0,  1,  2,  3,  0, -3, -2, -1 },
  { -1,  0,  1,  2,  3,  0, -3, -2 },
  { -2, -1,  0,  1,  2,  3,  0, -3 },
  { -3, -2, -1,  0,  1,  2,  3,  0 },
  {  0, -3, -2, -1,  0,  1,  2,  3 },
  {  3,  0, -3, -2, -1,  0,  1,  2 },
  {  2,  3,  0, -3, -2, -1,  0,  1 },
  {  1,  2,  3,  0, -3, -2, -1,  0 },
};


DiskII::DiskII()
{
}

void DiskII::setCycleCount(uint64_t cycle)
{
  cycle_count_ = cycle;

  // Track motor state changes
  if (motor_on_ && !motor_was_on_)
  {
    // Motor just turned on - reset tracking
    last_pc_log_cycle_ = cycle;
    pc_sample_count_ = 0;
  }
  motor_was_on_ = motor_on_;

  // If motor is on and we haven't seen a Q6L in a while (>50k cycles), log PC samples
  if (motor_on_ && last_q6l_cycle_ > 0)
  {
    uint64_t gap = cycle - last_q6l_cycle_;

    // Sample PC every 50,000 cycles during gap
    if (gap > 50000 && (cycle - last_pc_log_cycle_) >= 50000 && pc_sample_count_ < 20)
    {
      if (pc_callback_)
      {
        uint16_t pc = pc_callback_();
        std::cout << "[TRACE] Gap cycle " << cycle
                  << " (+" << gap << " since Q6L) | PC=$"
                  << std::hex << std::setw(4) << std::setfill('0') << pc << std::dec << std::endl;
        pc_sample_count_++;
      }
      last_pc_log_cycle_ = cycle;
    }
  }
}

void DiskII::reset()
{
  // Reset controller state but keep disks inserted
  selected_drive_ = 0;
  motor_on_ = false;
  accumulated_on_cycles_ = 0;
  last_motor_update_cycle_ = 0;
  current_phase_ = 0;
  data_latch_ = 0;
  q6_ = false;
  q7_ = false;

  // Reset drive head positions to track 0
  for (auto &drive : drives_)
  {
    drive.quarter_track = 0;
    drive.nibble_position = 0;
    drive.last_read_cycle = 0;
  }

  std::cout << "Disk II controller reset" << std::endl;
}

uint8_t DiskII::read(uint16_t address)
{
  uint8_t result = accessInternal(address);
  if (soft_switch_callback_)
  {
    soft_switch_callback_(address, false, result);
  }
  return result;
}

void DiskII::write(uint16_t address, uint8_t value)
{
  if (soft_switch_callback_)
  {
    soft_switch_callback_(address, true, value);
  }
  (void)value; // Disk II soft switches ignore written value
  accessInternal(address);
}

AddressRange DiskII::getAddressRange() const
{
  return {SLOT6_BASE, static_cast<uint16_t>(SLOT6_BASE + 0x0F)};
}

std::string DiskII::getName() const
{
  return "DiskII";
}

uint8_t DiskII::accessInternal(uint16_t address)
{
  // Extract the switch number (0-15)
  uint8_t sw = address & 0x0F;

  // DEBUG: Log all soft switch accesses
  static int access_count = 0;
  static const char* sw_names[] = {
    "PHASE0_OFF", "PHASE0_ON", "PHASE1_OFF", "PHASE1_ON",
    "PHASE2_OFF", "PHASE2_ON", "PHASE3_OFF", "PHASE3_ON",
    "MOTOR_OFF", "MOTOR_ON", "DRIVE1", "DRIVE2",
    "Q6L", "Q6H", "Q7L", "Q7H"
  };
  if (access_count < 0)  // Disable detailed soft switch logging to reduce noise
  {
    std::cout << "[DISK] Cycle " << cycle_count_
              << " | SW: " << sw_names[sw]
              << " | Track: " << (drives_[selected_drive_].quarter_track / 4)
              << " | Pos: " << drives_[selected_drive_].nibble_position
              << " | Motor: " << (motor_on_ ? "ON" : "OFF")
              << " | Q6: " << (q6_ ? "H" : "L")
              << " | Q7: " << (q7_ ? "H" : "L") << std::endl;
    access_count++;
  }

  // Process the soft switch
  switch (sw)
  {
  case PHASE0_OFF:
  case PHASE0_ON:
  case PHASE1_OFF:
  case PHASE1_ON:
  case PHASE2_OFF:
  case PHASE2_ON:
  case PHASE3_OFF:
  case PHASE3_ON:
    {
      // Phase handling using bitmask approach from apple2ts
      int phase = sw / 2;        // 0-3
      bool phase_on = sw & 1;    // odd = on, even = off

      uint8_t old_phase = current_phase_;
      if (phase_on)
      {
        current_phase_ |= (1 << phase);
      }
      else
      {
        current_phase_ &= ~(1 << phase);
      }

      if (old_phase != current_phase_ && access_count < 100)
      {
        std::cout << "[DISK] Phase change: 0x" << std::hex << (int)old_phase
                  << " -> 0x" << (int)current_phase_ << std::dec << std::endl;
      }

      // Only move head if motor is running
      if (motor_on_)
      {
        updateHeadPosition();
      }
    }
    break;

  case MOTOR_OFF:
    if (motor_on_)
    {
      // Accumulate the time motor was on
      uint64_t on_duration = cycle_count_ - last_motor_update_cycle_;
      accumulated_on_cycles_ += on_duration;
      last_motor_update_cycle_ = cycle_count_;

      DriveState &drive = drives_[selected_drive_];
      int track = drive.quarter_track / 4;
      std::cout << "[DISK] *** Motor OFF at cycle " << cycle_count_
                << " | Total on cycles: " << accumulated_on_cycles_
                << " | Track: " << track
                << " | Pos: " << drive.nibble_position << " ***" << std::endl;
    }
    motor_on_ = false;
    break;

  case MOTOR_ON:
    if (!motor_on_)
    {
      motor_on_ = true;
      last_motor_update_cycle_ = cycle_count_;
      std::cout << "[DISK] *** Motor ON at cycle " << cycle_count_
                << " | Accumulated cycles: " << accumulated_on_cycles_ << " ***" << std::endl;

      // Enable detailed position logging for next 100 reads after motor on
      static int motor_on_count = 0;
      if (motor_on_count < 5)
      {
        std::cout << "[DISK] !!! Motor ON #" << motor_on_count << " - enabling detailed position logging !!!" << std::endl;
        motor_on_count++;
      }
    }
    break;

  case DRIVE1_SEL:
    if (selected_drive_ != 0 && access_count < 100)
    {
      std::cout << "[DISK] Drive select: 2 -> 1" << std::endl;
    }
    selected_drive_ = 0;
    break;

  case DRIVE2_SEL:
    if (selected_drive_ != 1 && access_count < 100)
    {
      std::cout << "[DISK] Drive select: 1 -> 2" << std::endl;
    }
    selected_drive_ = 1;
    break;

  case Q6L:
    q6_ = false;
    // When Q6L is accessed and we're in read mode (Q7=false), read next nibble
    // This is the key trigger for reading data from disk
    if (!q7_)
    {
      static uint64_t last_q6l_cycle = 0;
      static int q6l_count = 0;
      static uint64_t gap_start_cycle = 0;
      static uint64_t last_pc_log_cycle = 0;
      static bool in_gap = false;

      uint64_t gap = (last_q6l_cycle > 0) ? (cycle_count_ - last_q6l_cycle) : 0;

      // Log when there's a large gap between Q6L accesses (> 100k cycles)
      if (gap > 100000 && q6l_count > 0)
      {
        std::cout << "[DISK] !!! Q6L GAP: " << gap << " cycles since last Q6L read !!!" << std::endl;
        gap_start_cycle = last_q6l_cycle;
        last_pc_log_cycle = last_q6l_cycle;
        in_gap = true;
      }

      // If we were in a gap, log summary
      if (in_gap)
      {
        std::cout << "[DISK] === Q6L GAP ENDED after " << (cycle_count_ - gap_start_cycle) << " cycles ===" << std::endl;
        in_gap = false;
      }

      data_latch_ = readNibble();
      last_q6l_cycle = cycle_count_;
      last_q6l_cycle_ = cycle_count_;  // Update member variable for gap tracking
      q6l_count++;
    }
    break;

  case Q6H:
    q6_ = true;
    break;

  case Q7L:
    q7_ = false;
    break;

  case Q7H:
    q7_ = true;
    break;
  }

  // Return value: even addresses return the data latch, odd addresses return floating bus
  // Exception: Q6H ($C0ED) in read mode returns write protect status
  if ((sw & 0x01) == 0)
  {
    // Even address - return data latch
    return data_latch_;
  }
  else
  {
    // Odd address - Q6H ($C0ED) in read mode senses write protect
    if (sw == Q6H && !q7_)
    {
      DriveState &drive = drives_[selected_drive_];
      if (drive.disk && drive.disk->isWriteProtected())
      {
        return 0x80; // Write protected (bit 7 set)
      }
      return 0x00; // Not protected
    }
    // Other odd addresses return floating bus (we return 0)
    return 0x00;
  }
}

void DiskII::updateHeadPosition()
{
  DriveState &drive = drives_[selected_drive_];

  // Look up the position from the current phase bitmask
  int position = MAGNET_TO_POSITION[current_phase_ & 0x0F];

  // DEBUG logging
  static int head_move_count = 0;
  if (head_move_count < 0)  // Disabled for now
  {
    std::cout << "[DISK] updateHeadPosition: phase_mask=0x" << std::hex << (int)current_phase_
              << std::dec << " position=" << position
              << " current_qtrack=" << drive.quarter_track << std::endl;
    head_move_count++;
  }

  // If invalid phase combination, no movement
  if (position < 0)
  {
    if (head_move_count < 50)
    {
      std::cout << "[DISK] Invalid phase combination, no movement" << std::endl;
    }
    return;
  }

  // Get last position from current quarter-track (bottom 3 bits)
  int last_position = drive.quarter_track & 7;

  // Look up direction of movement
  int direction = POSITION_TO_DIRECTION[last_position][position];

  if (head_move_count < 50 && direction != 0)
  {
    std::cout << "[DISK] Head movement: last_pos=" << last_position
              << " new_pos=" << position << " direction=" << direction << std::endl;
  }

  // Apply movement
  int new_quarter_track = drive.quarter_track + direction;

  // Clamp to valid range (0-139 quarter-tracks = tracks 0-34.75)
  // Maximum track is 34, so max quarter-track is 34*4 = 136
  // But we allow some overshoot like real drives
  if (new_quarter_track < 0)
  {
    new_quarter_track = 0;
  }
  else if (new_quarter_track > 139)
  {
    new_quarter_track = 139;
  }

  // Log track changes
  int old_track = drive.quarter_track / 4;
  int new_track = new_quarter_track / 4;
  if (old_track != new_track)
  {
    std::cout << "[DISK] TRACK CHANGE: " << old_track << " -> " << new_track
              << " (qtrack: " << drive.quarter_track << " -> " << new_quarter_track << ")" << std::endl;
  }

  drive.quarter_track = new_quarter_track;
}

uint8_t DiskII::readNibble()
{
  DriveState &drive = drives_[selected_drive_];

  // No disk inserted - return 0x00 (bit 7 clear = not ready)
  if (!drive.disk || !drive.disk->isLoaded())
  {
    return 0x00;
  }

  // Motor not on - return random data with bit 7 clear (not ready)
  if (!motor_on_)
  {
    return 0x7F; // Return non-zero but with bit 7 clear to signal not ready
  }

  // Get current track (quarter_track / 4 gives whole track number)
  int track = drive.quarter_track / 4;
  int track_size = drive.disk->getNibbleTrackSize(track);

  if (track_size == 0)
  {
    return 0x00;
  }

  // Calculate current position based on total accumulated on-time
  // Add current on-period if motor is running
  uint64_t total_on_cycles = accumulated_on_cycles_;
  if (motor_on_)
  {
    total_on_cycles += (cycle_count_ - last_motor_update_cycle_);
  }

  // Use fractional cycle tracking for precise timing
  // Real timing: 204,600 cycles/rotation ÷ 6656 nibbles = 30.7387 cycles/nibble
  // We use 30.74 by scaling: nibbles = (cycles * 100) / 3074
  uint64_t total_nibbles_elapsed = (total_on_cycles * 100) / 3074;
  int current_position = total_nibbles_elapsed % track_size;

  // Check if position has advanced since last read
  bool position_advanced = (current_position != drive.nibble_position);

  // Get the nibble at the current position
  uint8_t nibble = drive.disk->getNibble(track, current_position);

  // Update drive state
  drive.nibble_position = current_position;

  // DEBUG logging
  static int read_count = 0;
  static int last_position = -1;
  bool should_log = (read_count < 100);
  bool position_changed = (current_position != last_position);

  // Detect address field prologue (D5 AA 96) to identify sectors
  static uint8_t last_three[3] = {0, 0, 0};
  if (position_changed)
  {
    last_three[0] = last_three[1];
    last_three[1] = last_three[2];
    last_three[2] = nibble;

    static uint64_t last_sector_cycle = 0;
    if (last_three[0] == 0xD5 && last_three[1] == 0xAA && last_three[2] == 0x96)
    {
      int sector_num = drive.disk->findSectorAtPosition(track, current_position);
      uint64_t cycles_since_last = (last_sector_cycle > 0) ? (cycle_count_ - last_sector_cycle) : 0;
      std::cout << "[DISK] *** SECTOR " << sector_num << " (ADDRESS FIELD) | Cycle: " << cycle_count_
                << " | Track: " << track
                << " | Cycles since last: " << cycles_since_last << " ***" << std::endl;
      last_sector_cycle = cycle_count_;
    }

    // Detect data field prologue (D5 AA AD)
    if (last_three[0] == 0xD5 && last_three[1] == 0xAA && last_three[2] == 0xAD)
    {
      std::cout << "[DISK] *** DATA FIELD PROLOGUE | Track: " << track
                << " | Pos: " << current_position << " ***" << std::endl;
    }

    if (should_log && current_position < last_position)
    {
      std::cout << "[DISK] *** TRACK WRAP: position reset from " << last_position
                << " to " << current_position << " ***" << std::endl;
    }
  }

  if (should_log && read_count < 20 && position_changed)
  {
    std::cout << "[DISK] readNibble #" << read_count
              << " | Cycle: " << cycle_count_
              << " | Total on cycles: " << total_on_cycles
              << " | Total nibbles: " << total_nibbles_elapsed
              << " | Track: " << track
              << " | Pos: " << current_position
              << " | Nibble: 0x" << std::hex << (int)nibble << std::dec << std::endl;
    read_count++;
  }

  last_position = current_position;

  // Log nibble read if callback is set
  if (nibble_read_callback_ && position_changed)
  {
    nibble_read_callback_(nibble, track, current_position);
  }

  // Update the drive's position tracker
  drive.nibble_position = current_position;

  // Bit 7 is always set for valid GCR nibbles (which are in range 0x96-0xFF)
  // On real hardware, bit 7 indicates valid data in the shift register
  data_latch_ = nibble | 0x80;

  return data_latch_;
}

void DiskII::advancePosition()
{
  // Position advancement is now handled directly in readNibble()
}

bool DiskII::insertDisk(int drive_num, std::unique_ptr<DiskImage> image)
{
  if (drive_num < 0 || drive_num > 1)
  {
    return false;
  }

  drives_[drive_num].disk = std::move(image);
  drives_[drive_num].quarter_track = 0;
  drives_[drive_num].nibble_position = 0;
  drives_[drive_num].last_read_cycle = 0;

  std::cout << "Disk inserted in drive " << (drive_num + 1) << std::endl;
  return true;
}

void DiskII::ejectDisk(int drive_num)
{
  if (drive_num < 0 || drive_num > 1)
  {
    return;
  }

  drives_[drive_num].disk.reset();
  std::cout << "Disk ejected from drive " << (drive_num + 1) << std::endl;
}

bool DiskII::hasDisk(int drive_num) const
{
  if (drive_num < 0 || drive_num > 1)
  {
    return false;
  }

  return drives_[drive_num].disk != nullptr && drives_[drive_num].disk->isLoaded();
}

int DiskII::getCurrentTrack(int drive_num) const
{
  if (drive_num < 0 || drive_num > 1)
  {
    return 0;
  }

  return drives_[drive_num].quarter_track / 4;
}

int DiskII::getNibblePosition(int drive_num) const
{
  if (drive_num < 0 || drive_num > 1)
  {
    return 0;
  }

  return drives_[drive_num].nibble_position;
}
