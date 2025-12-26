#include "emulator/disk_formats/woz_disk_image.hpp"
#include "emulator/disk_formats/dos33_formatter.hpp"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

WozDiskImage::WozDiskImage()
{
  reset();
}

void WozDiskImage::reset()
{
  filepath_.clear();
  format_ = Format::Unknown;
  loaded_ = false;
  std::memset(&info_, 0, sizeof(info_));
  tmap_.fill(NO_TRACK);
  tracks_.clear();

  // Reset head positioning state
  phase_states_ = 0;
  quarter_track_ = 0;
  last_phase_ = 0;
  bit_position_ = 0;

  // Reset write support state
  backup_created_ = false;
}

bool WozDiskImage::load(const std::string &filepath)
{
  reset();

  // Read entire file into memory
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file)
  {
    return false;
  }

  auto file_size = file.tellg();
  if (file_size < static_cast<std::streamoff>(sizeof(WozHeader)))
  {
    return false;
  }

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
  if (!file.read(reinterpret_cast<char *>(file_data.data()), file_size))
  {
    return false;
  }
  file.close();

  // Validate header
  const auto *header = reinterpret_cast<const WozHeader *>(file_data.data());
  if (header->signature == WOZ1_SIGNATURE)
  {
    format_ = Format::WOZ1;
  }
  else if (header->signature == WOZ2_SIGNATURE)
  {
    format_ = Format::WOZ2;
  }
  else
  {
    return false;
  }

  // Validate magic bytes
  if (header->high_bits != 0xFF ||
      header->lfcrlf[0] != 0x0A ||
      header->lfcrlf[1] != 0x0D ||
      header->lfcrlf[2] != 0x0A)
  {
    return false;
  }

  // Parse chunks
  size_t offset = sizeof(WozHeader);
  bool has_info = false;
  bool has_tmap = false;
  bool has_trks = false;

  // Store TRKS chunk info for later processing (needs TMAP first)
  const uint8_t *trks_data = nullptr;
  uint32_t trks_size = 0;

  while (offset + sizeof(ChunkHeader) <= file_data.size())
  {
    const auto *chunk = reinterpret_cast<const ChunkHeader *>(file_data.data() + offset);
    const uint8_t *chunk_data = file_data.data() + offset + sizeof(ChunkHeader);

    if (offset + sizeof(ChunkHeader) + chunk->size > file_data.size())
    {
      break; // Chunk extends past end of file
    }

    switch (chunk->chunk_id)
    {
    case INFO_CHUNK_ID:
      if (!parseInfoChunk(chunk_data, chunk->size))
      {
        return false;
      }
      has_info = true;
      break;

    case TMAP_CHUNK_ID:
      if (!parseTmapChunk(chunk_data, chunk->size))
      {
        return false;
      }
      has_tmap = true;
      break;

    case TRKS_CHUNK_ID:
      // Save for later - need TMAP first
      trks_data = chunk_data;
      trks_size = chunk->size;
      has_trks = true;
      break;

    default:
      // Skip unknown chunks (META, WRIT, etc.)
      break;
    }

    offset += sizeof(ChunkHeader) + chunk->size;
  }

  // Validate required chunks
  if (!has_info || !has_tmap || !has_trks)
  {
    return false;
  }

  // Parse TRKS chunk (depends on format and TMAP)
  bool trks_ok = false;
  if (format_ == Format::WOZ1)
  {
    trks_ok = parseTrksChunkWoz1(trks_data, trks_size);
  }
  else
  {
    trks_ok = parseTrksChunkWoz2(file_data.data(), file_data.size(), trks_data, trks_size);
  }

  if (!trks_ok)
  {
    return false;
  }

  filepath_ = filepath;
  loaded_ = true;
  return true;
}

bool WozDiskImage::parseInfoChunk(const uint8_t *data, uint32_t size)
{
  // Minimum size is 60 bytes for WOZ2, but accept smaller for WOZ1
  if (size < 37)
  {
    return false;
  }

  // Copy the info structure (handle size differences)
  std::memset(&info_, 0, sizeof(info_));
  std::memcpy(&info_, data, std::min(size, static_cast<uint32_t>(sizeof(info_))));

  // Validate disk type
  if (info_.disk_type != 1 && info_.disk_type != 2)
  {
    return false;
  }

  return true;
}

bool WozDiskImage::parseTmapChunk(const uint8_t *data, uint32_t size)
{
  if (size < QUARTER_TRACK_COUNT)
  {
    return false;
  }

  std::memcpy(tmap_.data(), data, QUARTER_TRACK_COUNT);
  return true;
}

bool WozDiskImage::parseTrksChunkWoz1(const uint8_t *data, uint32_t size)
{
  // WOZ1 TRKS: each track is 6656 bytes of nibble data + metadata
  // Entry size: 6656 + 2 (bytes_used) + 2 (bit_count) + ... = 6646 total per entry
  static constexpr size_t WOZ1_NIBBLE_DATA_SIZE = 6656;
  static constexpr size_t WOZ1_ENTRY_SIZE = 6656 + 2 + 2 + 2 + 2; // nibbles + bytes_used + bit_count + splice fields

  // Count how many tracks we have
  size_t track_count = size / WOZ1_ENTRY_SIZE;
  tracks_.resize(track_count);

  for (size_t i = 0; i < track_count; i++)
  {
    const uint8_t *entry = data + i * WOZ1_ENTRY_SIZE;
    uint16_t bytes_used = entry[WOZ1_NIBBLE_DATA_SIZE] | (entry[WOZ1_NIBBLE_DATA_SIZE + 1] << 8);
    uint16_t bit_count = entry[WOZ1_NIBBLE_DATA_SIZE + 2] | (entry[WOZ1_NIBBLE_DATA_SIZE + 3] << 8);

    if (bytes_used > 0)
    {
      // Convert nibble data to bit data
      // Each nibble byte contains 8 bits
      tracks_[i].bit_count = bit_count;
      tracks_[i].bits.assign(entry, entry + bytes_used);
      tracks_[i].valid = true;
    }
  }

  return true;
}

bool WozDiskImage::parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                                       const uint8_t *trks_data, uint32_t trks_size)
{
  // WOZ2 TRKS: 160 track entries (8 bytes each) = 1280 bytes
  // Track data follows at block offsets specified in entries
  static constexpr size_t WOZ2_TRK_TABLE_SIZE = 160 * sizeof(Woz2TrackEntry);

  if (trks_size < WOZ2_TRK_TABLE_SIZE)
  {
    return false;
  }

  const auto *entries = reinterpret_cast<const Woz2TrackEntry *>(trks_data);

  // Find max track index referenced by TMAP
  int max_track_index = -1;
  for (int i = 0; i < QUARTER_TRACK_COUNT; i++)
  {
    if (tmap_[i] != NO_TRACK && tmap_[i] > max_track_index)
    {
      max_track_index = tmap_[i];
    }
  }

  if (max_track_index < 0)
  {
    return true; // No tracks - empty disk
  }

  tracks_.resize(max_track_index + 1);

  // Load each track referenced by TMAP
  for (int i = 0; i <= max_track_index; i++)
  {
    const auto &entry = entries[i];

    if (entry.starting_block == 0 || entry.block_count == 0)
    {
      continue; // Empty track
    }

    // Calculate offset in file (blocks start at offset 0 relative to start of file)
    size_t track_offset = static_cast<size_t>(entry.starting_block) * WOZ2_TRACK_BLOCK_SIZE;
    size_t track_size = static_cast<size_t>(entry.block_count) * WOZ2_TRACK_BLOCK_SIZE;

    if (track_offset + track_size > file_size)
    {
      continue; // Track extends past end of file
    }

    tracks_[i].bit_count = entry.bit_count;
    tracks_[i].bits.assign(file_data + track_offset,
                           file_data + track_offset + track_size);
    tracks_[i].valid = true;
  }

  return true;
}

bool WozDiskImage::isLoaded() const
{
  return loaded_;
}

const std::string &WozDiskImage::getFilepath() const
{
  return filepath_;
}

DiskImage::Format WozDiskImage::getFormat() const
{
  return format_;
}

int WozDiskImage::getTrackCount() const
{
  // Standard 5.25" disk has 35 tracks
  return 35;
}

// ===== Head Positioning =====

void WozDiskImage::setPhase(int phase, bool on)
{
  if (phase < 0 || phase > 3)
  {
    return;
  }

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

void WozDiskImage::updateHeadPosition(int phase)
{
  // The Disk II uses a 4-phase stepper motor with phases 2 quarter-tracks apart:
  //   Phase 0: quarter-tracks 0, 8, 16... (tracks 0, 2, 4...)
  //   Phase 1: quarter-tracks 2, 10, 18... (half-tracks 0.5, 2.5...)
  //   Phase 2: quarter-tracks 4, 12, 20... (tracks 1, 3, 5...)
  //   Phase 3: quarter-tracks 6, 14, 22... (half-tracks 1.5, 3.5...)
  //
  // From diskInfo.txt: "All even numbered tracks are positioned under phase 0
  // and all odd numbered tracks are under phase 2."
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
  if (phase_diff == 1)
  {
    // Stepping inward (toward higher track numbers)
    if (quarter_track_ < 158)  // Leave room for 2-step movement
    {
      quarter_track_ += 2;
    }
    else if (quarter_track_ < 159)
    {
      quarter_track_ = 159;  // Clamp to max
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
  // If phase_diff is 0, 2, or -2, it's not a valid step (head doesn't move)

  // Update last_phase_ for next step calculation
  last_phase_ = phase;
}

int WozDiskImage::getQuarterTrack() const
{
  return quarter_track_;
}

int WozDiskImage::getTrack() const
{
  return quarter_track_ / 4;
}

bool WozDiskImage::hasData() const
{
  if (quarter_track_ < 0 || quarter_track_ >= QUARTER_TRACK_COUNT)
  {
    return false;
  }
  return tmap_[quarter_track_] != NO_TRACK;
}

const WozDiskImage::TrackData *WozDiskImage::getTrackDataForQuarterTrack(int quarter_track) const
{
  if (quarter_track < 0 || quarter_track >= QUARTER_TRACK_COUNT)
  {
    return nullptr;
  }

  uint8_t track_index = tmap_[quarter_track];
  if (track_index == NO_TRACK || track_index >= tracks_.size())
  {
    return nullptr;
  }

  const TrackData &track = tracks_[track_index];
  return track.valid ? &track : nullptr;
}

const WozDiskImage::TrackData *WozDiskImage::getCurrentTrackData() const
{
  return getTrackDataForQuarterTrack(quarter_track_);
}

uint8_t WozDiskImage::readBitInternal() const
{
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0)
  {
    return 0;
  }

  // Wrap bit position to track length
  uint32_t pos = bit_position_ % track->bit_count;

  // Calculate byte and bit offsets
  uint32_t byte_offset = pos / 8;
  uint8_t bit_offset = 7 - (pos % 8); // MSB first

  if (byte_offset >= track->bits.size())
  {
    return 0;
  }

  return (track->bits[byte_offset] >> bit_offset) & 1;
}

void WozDiskImage::advanceBitPosition(uint64_t elapsed_cycles)
{
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0)
  {
    return;
  }

  // Calculate how many bits have passed
  uint32_t bits_elapsed = static_cast<uint32_t>(elapsed_cycles / CYCLES_PER_BIT);

  // Advance bit position (wrapping around track)
  bit_position_ = (bit_position_ + bits_elapsed) % track->bit_count;
}

uint8_t WozDiskImage::readNibble()
{
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0)
  {
    return 0;
  }

  // Read bits until we get a nibble (byte with high bit set)
  // The Disk II hardware shifts bits into a register until bit 7 is set
  uint8_t value = 0;
  int bits_read = 0;
  static constexpr int MAX_BITS = 64; // Safety limit

  while (bits_read < MAX_BITS)
  {
    uint8_t bit = readBitInternal();
    bit_position_ = (bit_position_ + 1) % track->bit_count;
    bits_read++;

    if (bit)
    {
      // Got a 1 bit - start/continue building nibble
      value = (value << 1) | 1;
    }
    else if (value != 0)
    {
      // Got a 0 bit after a 1 - continue building nibble
      value = value << 1;
    }
    // If value is 0 and bit is 0, we're still in sync bits - skip

    // Check if we have a complete nibble (bit 7 set)
    if (value & 0x80)
    {
      return value;
    }
  }

  // Timeout - return whatever we have
  return value;
}

bool WozDiskImage::isWriteProtected() const
{
  return info_.write_protected != 0;
}

std::string WozDiskImage::getFormatName() const
{
  switch (format_)
  {
  case Format::WOZ1:
    return "WOZ 1.0";
  case Format::WOZ2:
    return "WOZ 2.0";
  default:
    return "Unknown";
  }
}

uint8_t WozDiskImage::getDiskType() const
{
  return info_.disk_type;
}

uint8_t WozDiskImage::getOptimalBitTiming() const
{
  // Default to 32 (4 microseconds) if not specified
  return info_.optimal_bit_timing ? info_.optimal_bit_timing : 32;
}

std::string WozDiskImage::getDiskTypeString() const
{
  switch (info_.disk_type)
  {
  case 1:
    return "5.25\"";
  case 2:
    return "3.5\"";
  default:
    return "Unknown";
  }
}

std::string WozDiskImage::getCreator() const
{
  // Creator is a 32-byte field, may not be null-terminated
  return std::string(info_.creator, strnlen(info_.creator, sizeof(info_.creator)));
}

bool WozDiskImage::isSynchronized() const
{
  return info_.synchronized != 0;
}

bool WozDiskImage::isCleaned() const
{
  return info_.cleaned != 0;
}

uint8_t WozDiskImage::getBootSectorFormat() const
{
  return info_.boot_sector_format;
}

std::string WozDiskImage::getBootSectorFormatString() const
{
  switch (info_.boot_sector_format)
  {
  case 1:
    return "16-sector (DOS 3.3)";
  case 2:
    return "13-sector (DOS 3.2)";
  case 3:
    return "Both 13 & 16 sector";
  default:
    return "Unknown";
  }
}

uint8_t WozDiskImage::getDiskSides() const
{
  return info_.disk_sides ? info_.disk_sides : 1;
}

uint16_t WozDiskImage::getRequiredRAM() const
{
  return info_.required_ram;
}

// ===== Write Operations =====

WozDiskImage::TrackData *WozDiskImage::getMutableTrackData(int quarter_track)
{
  if (quarter_track < 0 || quarter_track >= QUARTER_TRACK_COUNT)
  {
    return nullptr;
  }

  uint8_t track_index = tmap_[quarter_track];
  if (track_index == NO_TRACK || track_index >= tracks_.size())
  {
    return nullptr;
  }

  TrackData &track = tracks_[track_index];
  return track.valid ? &track : nullptr;
}

WozDiskImage::TrackData *WozDiskImage::getMutableCurrentTrackData()
{
  return getMutableTrackData(quarter_track_);
}

void WozDiskImage::writeBitInternal(uint8_t bit)
{
  TrackData *track = getMutableCurrentTrackData();
  if (!track || track->bit_count == 0)
  {
    return;
  }

  // Wrap bit position to track length
  uint32_t pos = bit_position_ % track->bit_count;

  // Calculate byte and bit offsets
  uint32_t byte_offset = pos / 8;
  uint8_t bit_offset = 7 - (pos % 8); // MSB first

  if (byte_offset >= track->bits.size())
  {
    return;
  }

  // Clear the bit first, then set if needed
  track->bits[byte_offset] &= ~(1 << bit_offset);
  if (bit)
  {
    track->bits[byte_offset] |= (1 << bit_offset);
  }
}

void WozDiskImage::writeNibble(uint8_t nibble)
{
  if (!loaded_ || isWriteProtected())
  {
    return;
  }

  TrackData *track = getMutableCurrentTrackData();
  if (!track || track->bit_count == 0)
  {
    return;
  }

  // Create backup before first write
  if (!backup_created_)
  {
    createBackup();
  }

  // Write 8 bits, MSB first
  for (int i = 7; i >= 0; i--)
  {
    writeBitInternal((nibble >> i) & 1);
    bit_position_ = (bit_position_ + 1) % track->bit_count;
  }

  // Note: Don't save after every nibble - too slow!
  // Disk will be saved when ejected or when save() is called explicitly
}

bool WozDiskImage::createBackup()
{
  if (filepath_.empty() || backup_created_)
  {
    return backup_created_;
  }

  std::string backup_path = filepath_ + ".bak";

  // Read original file
  std::ifstream src(filepath_, std::ios::binary);
  if (!src)
  {
    return false;
  }

  // Write backup file
  std::ofstream dst(backup_path, std::ios::binary);
  if (!dst)
  {
    return false;
  }

  dst << src.rdbuf();
  dst.close();

  if (dst.fail())
  {
    return false;
  }

  backup_created_ = true;
  std::cout << "Created backup: " << backup_path << std::endl;
  return true;
}

bool WozDiskImage::save()
{
  if (filepath_.empty())
  {
    return false;
  }
  return saveAs(filepath_);
}

bool WozDiskImage::saveAs(const std::string &filepath)
{
  if (!loaded_)
  {
    return false;
  }

  std::vector<uint8_t> file_data = buildWozFile();
  if (file_data.empty())
  {
    return false;
  }

  std::ofstream file(filepath, std::ios::binary);
  if (!file)
  {
    return false;
  }

  file.write(reinterpret_cast<const char *>(file_data.data()), file_data.size());
  file.close();

  if (file.fail())
  {
    return false;
  }

  filepath_ = filepath;
  return true;
}

std::vector<uint8_t> WozDiskImage::buildWozFile() const
{
  std::vector<uint8_t> file_data;

  // Build header (12 bytes) - will fill in CRC at the end
  WozHeader header{};
  header.signature = (format_ == Format::WOZ1) ? WOZ1_SIGNATURE : WOZ2_SIGNATURE;
  header.high_bits = 0xFF;
  header.lfcrlf[0] = 0x0A;
  header.lfcrlf[1] = 0x0D;
  header.lfcrlf[2] = 0x0A;
  header.crc32 = 0; // Will calculate after all data is built

  file_data.resize(sizeof(WozHeader));
  std::memcpy(file_data.data(), &header, sizeof(header));

  // Build INFO chunk
  ChunkHeader info_header{};
  info_header.chunk_id = INFO_CHUNK_ID;
  info_header.size = 60; // WOZ2 INFO size

  size_t info_start = file_data.size();
  file_data.resize(info_start + sizeof(ChunkHeader) + 60);
  std::memcpy(file_data.data() + info_start, &info_header, sizeof(info_header));
  std::memcpy(file_data.data() + info_start + sizeof(ChunkHeader), &info_, 60);

  // Build TMAP chunk
  ChunkHeader tmap_header{};
  tmap_header.chunk_id = TMAP_CHUNK_ID;
  tmap_header.size = QUARTER_TRACK_COUNT;

  size_t tmap_start = file_data.size();
  file_data.resize(tmap_start + sizeof(ChunkHeader) + QUARTER_TRACK_COUNT);
  std::memcpy(file_data.data() + tmap_start, &tmap_header, sizeof(tmap_header));
  std::memcpy(file_data.data() + tmap_start + sizeof(ChunkHeader), tmap_.data(), QUARTER_TRACK_COUNT);

  // Build TRKS chunk based on format
  if (format_ == Format::WOZ1)
  {
    std::vector<uint8_t> trks_data = buildTrksChunkWoz1();

    ChunkHeader trks_header{};
    trks_header.chunk_id = TRKS_CHUNK_ID;
    trks_header.size = static_cast<uint32_t>(trks_data.size());

    size_t trks_start = file_data.size();
    file_data.resize(trks_start + sizeof(ChunkHeader) + trks_data.size());
    std::memcpy(file_data.data() + trks_start, &trks_header, sizeof(trks_header));
    std::memcpy(file_data.data() + trks_start + sizeof(ChunkHeader), trks_data.data(), trks_data.size());
  }
  else // WOZ2
  {
    // WOZ2 requires track data to be stored in 512-byte blocks
    // The TRKS chunk contains 160 entries pointing to block offsets
    std::vector<uint8_t> trks_entries = buildTrksChunkWoz2();

    ChunkHeader trks_header{};
    trks_header.chunk_id = TRKS_CHUNK_ID;
    trks_header.size = static_cast<uint32_t>(trks_entries.size());

    size_t trks_start = file_data.size();
    file_data.resize(trks_start + sizeof(ChunkHeader) + trks_entries.size());
    std::memcpy(file_data.data() + trks_start, &trks_header, sizeof(trks_header));
    std::memcpy(file_data.data() + trks_start + sizeof(ChunkHeader), trks_entries.data(), trks_entries.size());

    // Now append track data blocks
    // Calculate starting block (must be after all chunks, aligned to 512)
    size_t current_size = file_data.size();
    size_t starting_block = (current_size + 511) / 512;
    size_t aligned_start = starting_block * 512;

    // Pad to block boundary
    if (aligned_start > current_size)
    {
      file_data.resize(aligned_start, 0);
    }

    // Append track data and update TRKS entries
    // Note: We must recalculate entries pointer after each resize since
    // vector reallocation invalidates pointers
    size_t entries_offset = trks_start + sizeof(ChunkHeader);

    for (size_t i = 0; i < tracks_.size() && i < 160; i++)
    {
      const TrackData &track = tracks_[i];
      if (!track.valid || track.bit_count == 0)
      {
        continue;
      }

      // Calculate blocks needed
      size_t track_bytes = (track.bit_count + 7) / 8;
      size_t blocks_needed = (track_bytes + 511) / 512;
      size_t padded_size = blocks_needed * 512;

      // Update entry (recalculate pointer after potential reallocation)
      auto *entries = reinterpret_cast<Woz2TrackEntry *>(
          file_data.data() + entries_offset);
      entries[i].starting_block = static_cast<uint16_t>(file_data.size() / 512);
      entries[i].block_count = static_cast<uint16_t>(blocks_needed);
      entries[i].bit_count = track.bit_count;

      // Append track data (padded to block boundary)
      size_t track_start = file_data.size();
      file_data.resize(track_start + padded_size, 0);
      std::memcpy(file_data.data() + track_start, track.bits.data(),
                  std::min(track.bits.size(), track_bytes));
    }
  }

  // Calculate and set CRC32 (over all data after CRC field)
  uint32_t crc = calculateCRC32(file_data.data() + 12, file_data.size() - 12);
  std::memcpy(file_data.data() + 8, &crc, sizeof(crc));

  return file_data;
}

std::vector<uint8_t> WozDiskImage::buildTrksChunkWoz1() const
{
  // WOZ1: each track entry is 6656 bytes of nibble data + 8 bytes metadata
  static constexpr size_t WOZ1_NIBBLE_DATA_SIZE = 6656;
  static constexpr size_t WOZ1_ENTRY_SIZE = 6656 + 2 + 2 + 2 + 2;

  std::vector<uint8_t> trks_data;
  trks_data.resize(tracks_.size() * WOZ1_ENTRY_SIZE, 0);

  for (size_t i = 0; i < tracks_.size(); i++)
  {
    const TrackData &track = tracks_[i];
    uint8_t *entry = trks_data.data() + i * WOZ1_ENTRY_SIZE;

    if (track.valid)
    {
      // Copy track data (up to 6656 bytes)
      size_t copy_size = std::min(track.bits.size(), WOZ1_NIBBLE_DATA_SIZE);
      std::memcpy(entry, track.bits.data(), copy_size);

      // Set bytes_used and bit_count (little-endian)
      uint16_t bytes_used = static_cast<uint16_t>(copy_size);
      uint16_t bit_count = static_cast<uint16_t>(track.bit_count & 0xFFFF);
      entry[WOZ1_NIBBLE_DATA_SIZE + 0] = bytes_used & 0xFF;
      entry[WOZ1_NIBBLE_DATA_SIZE + 1] = (bytes_used >> 8) & 0xFF;
      entry[WOZ1_NIBBLE_DATA_SIZE + 2] = bit_count & 0xFF;
      entry[WOZ1_NIBBLE_DATA_SIZE + 3] = (bit_count >> 8) & 0xFF;
      // Splice point and nibble fields left as 0
    }
  }

  return trks_data;
}

std::vector<uint8_t> WozDiskImage::buildTrksChunkWoz2() const
{
  // WOZ2 TRKS: 160 entries x 8 bytes = 1280 bytes
  // Entries will be updated later when we know actual block offsets
  std::vector<uint8_t> entries(160 * sizeof(Woz2TrackEntry), 0);
  return entries;
}

// Standard CRC32 table (polynomial 0xEDB88320)
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCd66BCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D};

uint32_t WozDiskImage::calculateCRC32(const uint8_t *data, size_t size)
{
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < size; i++)
  {
    crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFF;
}

// ===== Static Factory Methods =====

std::unique_ptr<WozDiskImage> WozDiskImage::createEmptyDOS33Disk(
    const std::string &filepath,
    uint8_t volume_number)
{
  auto disk = std::make_unique<WozDiskImage>();

  // Set up as WOZ2 format
  disk->format_ = Format::WOZ2;
  disk->filepath_ = filepath;

  // Initialize INFO chunk
  std::memset(&disk->info_, 0, sizeof(disk->info_));
  disk->info_.version = 2;
  disk->info_.disk_type = 1;            // 5.25"
  disk->info_.write_protected = 0;      // Not write protected
  disk->info_.synchronized = 0;
  disk->info_.cleaned = 1;
  std::strncpy(disk->info_.creator, "A2E Emulator", sizeof(disk->info_.creator) - 1);
  disk->info_.disk_sides = 1;
  disk->info_.boot_sector_format = 1;   // 16-sector (DOS 3.3)
  disk->info_.optimal_bit_timing = 32;  // 4us bit cells

  // Generate formatted disk data
  DOS33DiskFormatter formatter(volume_number);
  auto track_data = formatter.generateNibblizedTracks();
  auto bit_counts = formatter.getTrackBitCounts();

  // Initialize TMAP: map quarter-tracks to tracks
  // For a 35-track disk, only whole tracks (every 4 quarter-tracks)
  disk->tmap_.fill(NO_TRACK);
  for (int track = 0; track < 35; track++)
  {
    disk->tmap_[track * 4] = static_cast<uint8_t>(track);
  }

  // Copy track data and calculate largest track size
  disk->tracks_.resize(35);
  uint16_t largest_block_count = 0;
  for (int track = 0; track < 35; track++)
  {
    disk->tracks_[track].bits = std::move(track_data[track]);
    disk->tracks_[track].bit_count = bit_counts[track];
    disk->tracks_[track].valid = true;

    // Calculate block count for this track
    size_t track_bytes = (bit_counts[track] + 7) / 8;
    uint16_t blocks = static_cast<uint16_t>((track_bytes + 511) / 512);
    if (blocks > largest_block_count)
    {
      largest_block_count = blocks;
    }
  }

  // Set largest_track in INFO chunk (WOZ2 requirement)
  disk->info_.largest_track = largest_block_count;

  // Mark as loaded before saving (saveAs checks this flag)
  disk->loaded_ = true;
  disk->backup_created_ = false; // New disk, no backup needed yet

  // Save to file
  if (!disk->saveAs(filepath))
  {
    std::cerr << "Failed to save new disk image: " << filepath << std::endl;
    return nullptr;
  }

  std::cout << "Created new DOS 3.3 formatted disk: " << filepath << std::endl;

  return disk;
}
