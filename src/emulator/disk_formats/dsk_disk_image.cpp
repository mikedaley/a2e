#include "emulator/disk_formats/dsk_disk_image.hpp"
#include "emulator/disk_formats/gcr_encoding.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

// DOS 3.3 logical to physical sector mapping
// When reading a DSK file, logical sector N is at file offset N * 256
// But on disk, sectors are interleaved for performance
static constexpr std::array<int, 16> DOS_LOGICAL_TO_PHYSICAL = {
    0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15
};

// Reverse mapping: physical to logical
static constexpr std::array<int, 16> DOS_PHYSICAL_TO_LOGICAL = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
};

// ProDOS logical to physical sector mapping
static constexpr std::array<int, 16> PRODOS_LOGICAL_TO_PHYSICAL = {
    0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15
};

// ProDOS reverse mapping
static constexpr std::array<int, 16> PRODOS_PHYSICAL_TO_LOGICAL = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

// 6-and-2 decoding table (reverse of ENCODE_6_AND_2)
static constexpr std::array<int8_t, 256> DECODE_6_AND_2 = []() {
  std::array<int8_t, 256> table{};
  for (int i = 0; i < 256; i++) {
    table[i] = -1;  // Invalid by default
  }
  // Fill in valid mappings from the encode table
  for (int i = 0; i < 64; i++) {
    table[GCR::ENCODE_6_AND_2[i]] = static_cast<int8_t>(i);
  }
  return table;
}();

DskDiskImage::DskDiskImage()
{
  sector_data_.fill(0);
}

bool DskDiskImage::load(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file)
  {
    std::cerr << "DSK: Failed to open file: " << filepath << std::endl;
    return false;
  }

  // Check file size
  auto size = file.tellg();
  if (size != DISK_SIZE)
  {
    std::cerr << "DSK: Invalid file size: " << size << " (expected " << DISK_SIZE << ")" << std::endl;
    return false;
  }

  // Read entire file
  file.seekg(0);
  file.read(reinterpret_cast<char *>(sector_data_.data()), DISK_SIZE);
  if (!file)
  {
    std::cerr << "DSK: Failed to read file data" << std::endl;
    return false;
  }

  filepath_ = filepath;
  loaded_ = true;
  modified_ = false;

  // Determine format from extension
  std::string ext = filepath;
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext.ends_with(".po"))
  {
    format_ = Format::PO;
  }
  else if (ext.ends_with(".do"))
  {
    format_ = Format::DO;
  }
  else
  {
    format_ = Format::DSK;  // Default to DOS order
  }

  // Invalidate all nibble tracks (will be regenerated on demand)
  for (auto &track : nibble_tracks_)
  {
    track.valid = false;
    track.dirty = false;
    track.nibbles.clear();
  }

  // Reset head position
  quarter_track_ = 0;
  phase_states_ = 0;
  last_phase_ = 0;  // Reset to phase 0 for correct stepper tracking
  nibble_position_ = 0;
  shift_register_ = 0;

  std::cout << "DSK: Loaded " << getFormatName() << " image: " << filepath << std::endl;
  return true;
}

std::string DskDiskImage::getFormatName() const
{
  switch (format_)
  {
  case Format::DSK:
    return "DSK (DOS order)";
  case Format::DO:
    return "DO (DOS order)";
  case Format::PO:
    return "PO (ProDOS order)";
  default:
    return "Unknown";
  }
}

int DskDiskImage::getPhysicalSector(int logical_sector) const
{
  if (logical_sector < 0 || logical_sector >= SECTORS_PER_TRACK)
    return 0;

  if (format_ == Format::PO)
  {
    return PRODOS_LOGICAL_TO_PHYSICAL[logical_sector];
  }
  else
  {
    return DOS_LOGICAL_TO_PHYSICAL[logical_sector];
  }
}

int DskDiskImage::getLogicalSector(int physical_sector) const
{
  if (physical_sector < 0 || physical_sector >= SECTORS_PER_TRACK)
    return 0;

  if (format_ == Format::PO)
  {
    return PRODOS_PHYSICAL_TO_LOGICAL[physical_sector];
  }
  else
  {
    return DOS_PHYSICAL_TO_LOGICAL[physical_sector];
  }
}

void DskDiskImage::nibblizeTrack(int track)
{
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  nt.nibbles.clear();
  nt.nibbles.reserve(NIBBLES_PER_TRACK);


  // Build each sector using the exact structure from the working version
  for (int physical_sector = 0; physical_sector < SECTORS_PER_TRACK; physical_sector++)
  {
    // Map physical sector to DOS logical sector
    int dos_sector = getLogicalSector(physical_sector);

    // Get sector data
    int offset = (track * SECTORS_PER_TRACK + dos_sector) * BYTES_PER_SECTOR;
    const uint8_t *data = &sector_data_[offset];


    // Gap 1 (first sector) or Gap 3 (between sectors)
    int gap;
    if (physical_sector == 0)
    {
      gap = 0x80; // Gap 1: 128 bytes
    }
    else
    {
      gap = (track == 0) ? 0x28 : 0x26; // Gap 3: 40 or 38 bytes
    }
    for (int i = 0; i < gap; ++i)
    {
      nt.nibbles.push_back(0xFF);
    }

    // === Address Field ===
    // Prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0x96);

    // 4-and-4 encoded values
    auto encode44 = [&](uint8_t val) {
      nt.nibbles.push_back((val >> 1) | 0xAA);
      nt.nibbles.push_back(val | 0xAA);
    };

    uint8_t checksum = volume_number_ ^ track ^ physical_sector;
    encode44(volume_number_);
    encode44(track);
    encode44(physical_sector);
    encode44(checksum);

    // Epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 2: 5 bytes
    for (int i = 0; i < 5; ++i)
    {
      nt.nibbles.push_back(0xFF);
    }

    // === Data Field ===
    // Prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xAD);

    // 6-and-2 encode the sector data
    auto encoded = GCR::encode6and2(data);
    nt.nibbles.insert(nt.nibbles.end(), encoded.begin(), encoded.end());

    // Epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 3 end: 1 byte
    nt.nibbles.push_back(0xFF);
  }

  // Pad or truncate to standard track size
  while (nt.nibbles.size() < NIBBLES_PER_TRACK)
  {
    nt.nibbles.push_back(0xFF);
  }
  if (nt.nibbles.size() > NIBBLES_PER_TRACK)
  {
    nt.nibbles.resize(NIBBLES_PER_TRACK);
  }

  nt.valid = true;
  nt.dirty = false;
}

uint8_t DskDiskImage::decode4and4(uint8_t odd, uint8_t even)
{
  // Reverse of encode4and4:
  // odd has bits 7,5,3,1 of original in positions 6,4,2,0 (masked with 0x55, OR'd with 0xAA)
  // even has bits 6,4,2,0 of original in positions 6,4,2,0
  uint8_t result = ((odd << 1) & 0xAA) | (even & 0x55);
  return result;
}

bool DskDiskImage::decode6and2(const uint8_t *encoded, uint8_t *output)
{
  // Decode 343 nibbles back to 256 bytes
  // First, convert disk nibbles to 6-bit values
  uint8_t buffer[342];

  // XOR decode (reverse of encode)
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++)
  {
    int8_t decoded = DECODE_6_AND_2[encoded[i]];
    if (decoded < 0)
    {
      return false;  // Invalid nibble
    }
    buffer[i] = decoded ^ prev;
    prev = buffer[i];
  }

  // Verify checksum
  int8_t checksum_decoded = DECODE_6_AND_2[encoded[342]];
  if (checksum_decoded < 0 || (prev & 0x3F) != (checksum_decoded & 0x3F))
  {
    // Checksum mismatch - still try to decode
    // Some disk images have minor errors
  }

  // Reconstruct 256 bytes from auxiliary (86) and primary (256) buffers
  for (int i = 0; i < 256; i++)
  {
    // High 6 bits from primary buffer
    uint8_t high = buffer[86 + i] << 2;

    // Low 2 bits from auxiliary buffer
    uint8_t aux_byte = buffer[i % 86];
    int shift = (i / 86) * 2;
    uint8_t low = (aux_byte >> shift) & 0x03;

    output[i] = high | low;
  }

  return true;
}

void DskDiskImage::denibblizeTrack(int track)
{
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  if (!nt.valid || !nt.dirty)
    return;

  const auto &nibbles = nt.nibbles;
  size_t pos = 0;
  size_t size = nibbles.size();

  // Find and decode each sector
  while (pos < size)
  {
    // Look for address field prologue: D5 AA 96
    bool found_addr = false;
    while (pos + 3 < size)
    {
      if (nibbles[pos] == 0xD5 && nibbles[pos + 1] == 0xAA && nibbles[pos + 2] == 0x96)
      {
        found_addr = true;
        pos += 3;
        break;
      }
      pos++;
    }

    if (!found_addr)
      break;

    // Read address field (4-and-4 encoded: volume, track, sector, checksum)
    if (pos + 8 > size)
      break;

    uint8_t volume = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t addr_track = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t sector = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t checksum = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;

    // Verify address checksum
    if ((volume ^ addr_track ^ sector) != checksum)
    {
      continue;  // Invalid address field
    }

    // Verify track number matches
    if (addr_track != track)
    {
      continue;  // Wrong track
    }

    // Skip address epilogue and look for data prologue: D5 AA AD
    bool found_data = false;
    size_t search_limit = pos + 50;  // Don't search too far
    while (pos + 3 < size && pos < search_limit)
    {
      if (nibbles[pos] == 0xD5 && nibbles[pos + 1] == 0xAA && nibbles[pos + 2] == 0xAD)
      {
        found_data = true;
        pos += 3;
        break;
      }
      pos++;
    }

    if (!found_data)
      continue;

    // Read 343 nibbles of data field
    if (pos + 343 > size)
      break;

    // Decode the sector data
    uint8_t decoded[256];
    if (decode6and2(&nibbles[pos], decoded))
    {
      // Write to sector data array
      int log_sector = getLogicalSector(sector);
      int offset = (track * SECTORS_PER_TRACK + log_sector) * BYTES_PER_SECTOR;
      std::memcpy(&sector_data_[offset], decoded, BYTES_PER_SECTOR);
    }

    pos += 343;
  }

  nt.dirty = false;
  modified_ = true;
}

void DskDiskImage::setPhase(int phase, bool on)
{
  if (phase < 0 || phase > 3)
    return;

  uint8_t phase_bit = 1 << phase;

  if (on)
  {
    phase_states_ |= phase_bit;
    // Update head position when a phase is activated
    updateHeadPosition(phase);
  }
  else
  {
    phase_states_ &= ~phase_bit;
    // Turning off a phase doesn't cause stepping
  }
}

void DskDiskImage::updateHeadPosition(int phase)
{
  // The Disk II uses a 4-phase stepper motor with phases 2 quarter-tracks apart:
  //   Phase 0: quarter-tracks 0, 8, 16... (tracks 0, 2, 4...)
  //   Phase 1: quarter-tracks 2, 10, 18... (half-tracks 0.5, 2.5...)
  //   Phase 2: quarter-tracks 4, 12, 20... (tracks 1, 3, 5...)
  //   Phase 3: quarter-tracks 6, 14, 22... (half-tracks 1.5, 3.5...)
  //
  // Each adjacent phase change moves the head by 2 quarter-tracks (1 half-track).
  // When two adjacent phases are on, head settles at odd quarter-track between.

  // Calculate step direction based on phase difference from last activated phase
  int phase_diff = phase - last_phase_;

  // Normalize to handle wrap-around
  if (phase_diff == 3)
    phase_diff = -1; // 0->3 is stepping backward
  if (phase_diff == -3)
    phase_diff = 1; // 3->0 is stepping forward

  // Only move if the phase change is a valid single step (+1 or -1)
  // Each valid step moves 2 quarter-tracks (1 half-track)
  // Max quarter-track for 35 tracks is (35 * 4) - 1 = 139
  constexpr int MAX_QUARTER_TRACK = (TRACKS * 4) - 1;

  if (phase_diff == 1)
  {
    // Stepping inward (toward higher track numbers)
    if (quarter_track_ < MAX_QUARTER_TRACK - 1)  // Leave room for 2-step movement
    {
      quarter_track_ += 2;
    }
    else if (quarter_track_ < MAX_QUARTER_TRACK)
    {
      quarter_track_ = MAX_QUARTER_TRACK;  // Clamp to max
    }
  }
  else if (phase_diff == -1)
  {
    // Stepping outward (toward track 0)
    if (quarter_track_ > 1)
    {
      quarter_track_ -= 2;
    }
    else if (quarter_track_ > 0)
    {
      quarter_track_ = 0;  // Clamp to min
    }
  }

  last_phase_ = phase;
}

bool DskDiskImage::hasData() const
{
  int track = quarter_track_ / 4;
  return track >= 0 && track < TRACKS;
}

void DskDiskImage::ensureTrackNibblized()
{
  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
  {
    std::cerr << "DSK: ensureTrackNibblized - invalid track " << track
              << " (quarter_track=" << quarter_track_ << ")" << std::endl;
    return;
  }

  if (!nibble_tracks_[track].valid)
  {
    nibblizeTrack(track);
  }
}

void DskDiskImage::advanceBitPosition(uint64_t current_cycles)
{
  if (!loaded_)
    return;

  // Calculate elapsed cycles since last update
  if (current_cycles <= last_cycle_count_)
  {
    last_cycle_count_ = current_cycles;
    return;
  }

  uint64_t elapsed = current_cycles - last_cycle_count_;
  last_cycle_count_ = current_cycles;

  // Disk spins at ~300 RPM = 5 revolutions/second
  // At 1.023 MHz, one revolution = ~204,600 cycles
  // With 6656 nibbles per track, each nibble = ~30.7 cycles
  // But nibbles are 8 bits, and each bit = ~4 cycles (4 microseconds)
  // So each nibble takes ~32 cycles

  // Advance nibble position based on elapsed cycles
  // We use integer math for efficiency
  constexpr uint64_t CYCLES_PER_NIBBLE = 32;

  ensureTrackNibblized();

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
    return;

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty())
    return;

  // Advance position
  uint64_t nibbles_elapsed = elapsed / CYCLES_PER_NIBBLE;
  nibble_position_ = (nibble_position_ + nibbles_elapsed) % nt.nibbles.size();
}

uint8_t DskDiskImage::readNibble()
{
  if (!loaded_)
    return 0xFF;  // Return sync byte pattern when not loaded

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
    return 0xFF;  // Return sync byte pattern for invalid track

  ensureTrackNibblized();

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty())
    return 0xFF;  // Return sync byte pattern if track not ready

  // Read nibble at current position
  uint8_t nibble = nt.nibbles[nibble_position_];

  // Advance to next nibble
  nibble_position_ = (nibble_position_ + 1) % nt.nibbles.size();

  // All valid disk nibbles must have bit 7 set
  // This is guaranteed by GCR encoding, but verify for safety
  return nibble | 0x80;
}

void DskDiskImage::writeNibble(uint8_t nibble)
{
  if (!loaded_ || write_protected_)
    return;

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
  {
    static int bad_track_count = 0;
    if (++bad_track_count <= 10)
    {
      std::cout << "DSK: writeNibble invalid track=" << track
                << " (quarter_track=" << quarter_track_ << ")" << std::endl;
    }
    return;
  }

  ensureTrackNibblized();

  auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty())
  {
    std::cout << "DSK: writeNibble track " << track << " not valid" << std::endl;
    return;
  }

  // Write nibble at current position
  nt.nibbles[nibble_position_] = nibble;
  nt.dirty = true;

  // Advance to next nibble
  nibble_position_ = (nibble_position_ + 1) % nt.nibbles.size();
}

bool DskDiskImage::save()
{
  return saveAs(filepath_);
}

bool DskDiskImage::saveAs(const std::string &filepath)
{
  if (!loaded_)
    return false;

  // Denibblize any dirty tracks back to sector data
  for (int t = 0; t < TRACKS; t++)
  {
    if (nibble_tracks_[t].dirty)
    {
      denibblizeTrack(t);
    }
  }

  // Write sector data to file
  std::ofstream file(filepath, std::ios::binary);
  if (!file)
  {
    std::cerr << "DSK: Failed to open file for writing: " << filepath << std::endl;
    return false;
  }

  file.write(reinterpret_cast<const char *>(sector_data_.data()), DISK_SIZE);
  if (!file)
  {
    std::cerr << "DSK: Failed to write file data" << std::endl;
    return false;
  }

  filepath_ = filepath;
  modified_ = false;

  // Clear dirty flags
  for (auto &nt : nibble_tracks_)
  {
    nt.dirty = false;
  }

  std::cout << "DSK: Saved to " << filepath << std::endl;
  return true;
}

int DskDiskImage::findNextSector(int start_pos) const
{
  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
    return -1;

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid)
    return -1;

  size_t size = nt.nibbles.size();
  size_t pos = start_pos;

  // Search for address prologue
  for (size_t i = 0; i < size; i++)
  {
    size_t idx = (pos + i) % size;
    if (idx + 2 < size &&
        nt.nibbles[idx] == 0xD5 &&
        nt.nibbles[idx + 1] == 0xAA &&
        nt.nibbles[idx + 2] == 0x96)
    {
      return static_cast<int>(idx);
    }
  }

  return -1;
}
