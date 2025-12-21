#include "disk2.hpp"
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

void DiskII::reset()
{
  // Reset controller state but keep disks inserted
  selected_drive_ = 0;
  motor_on_ = false;
  motor_on_cycle_ = 0;
  current_phase_ = 0;
  data_latch_ = 0;
  q6_ = false;
  q7_ = false;
  
  // Reset drive head positions to track 0
  for (auto &drive : drives_)
  {
    drive.quarter_track = 0;
    drive.nibble_position = 0;
    drive.last_access_cycle = 0;
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
      
      if (phase_on)
      {
        current_phase_ |= (1 << phase);
      }
      else
      {
        current_phase_ &= ~(1 << phase);
      }
      
      // Only move head if motor is running
      if (motor_on_)
      {
        updateHeadPosition();
      }
    }
    break;

  case MOTOR_OFF:
    motor_on_ = false;
    break;

  case MOTOR_ON:
    if (!motor_on_)
    {
      motor_on_ = true;
      motor_on_cycle_ = cycle_count_;
    }
    break;

  case DRIVE1_SEL:
    selected_drive_ = 0;
    break;

  case DRIVE2_SEL:
    selected_drive_ = 1;
    break;

  case Q6L:
    q6_ = false;
    // When Q6L is accessed and we're in read mode (Q7=false), read next nibble
    // This is the key trigger for reading data from disk
    if (!q7_)
    {
      data_latch_ = readNibble();
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
  
  // If invalid phase combination, no movement
  if (position < 0)
  {
    return;
  }
  
  // Get last position from current quarter-track (bottom 3 bits)
  int last_position = drive.quarter_track & 7;
  
  // Look up direction of movement
  int direction = POSITION_TO_DIRECTION[last_position][position];
  
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
  
  drive.quarter_track = new_quarter_track;
}

uint8_t DiskII::readNibble()
{
  DriveState &drive = drives_[selected_drive_];

  // No disk inserted - return 0x00 (bit 7 clear = invalid nibble)
  if (!drive.disk || !drive.disk->isLoaded())
  {
    return 0x00;
  }

  // Motor not on - return 0x00
  if (!motor_on_)
  {
    return 0x00;
  }

  // Get current track (quarter_track / 4 gives whole track number)
  int track = drive.quarter_track / 4;
  int track_size = drive.disk->getNibbleTrackSize();
  
  // Get current nibble
  uint8_t nibble = drive.disk->getNibble(track, drive.nibble_position);

  // Log nibble read if callback is set
  if (nibble_read_callback_)
  {
    nibble_read_callback_(nibble, track, drive.nibble_position);
  }

  // Advance to next nibble position (wrap at end of track)
  if (++drive.nibble_position >= track_size)
  {
    drive.nibble_position = 0;
  }

  return nibble;
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
  drives_[drive_num].last_access_cycle = 0;

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
