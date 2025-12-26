#include "emulator/disk_formats/woz_disk_image.hpp"
#include <cstring>
#include <fstream>

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

bool WozDiskImage::hasQuarterTrack(int quarter_track) const
{
  if (quarter_track < 0 || quarter_track >= QUARTER_TRACK_COUNT)
  {
    return false;
  }
  return tmap_[quarter_track] != NO_TRACK;
}

uint32_t WozDiskImage::getTrackBitCount(int quarter_track) const
{
  const TrackData *track = getTrackData(quarter_track);
  return track ? track->bit_count : 0;
}

const WozDiskImage::TrackData *WozDiskImage::getTrackData(int quarter_track) const
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

uint8_t WozDiskImage::readBit(int quarter_track, uint32_t bit_position) const
{
  const TrackData *track = getTrackData(quarter_track);
  if (!track || track->bit_count == 0)
  {
    return 0;
  }

  // Wrap bit position to track length
  uint32_t pos = bit_position % track->bit_count;

  // Calculate byte and bit offsets
  uint32_t byte_offset = pos / 8;
  uint8_t bit_offset = 7 - (pos % 8); // MSB first

  if (byte_offset >= track->bits.size())
  {
    return 0;
  }

  return (track->bits[byte_offset] >> bit_offset) & 1;
}

uint8_t WozDiskImage::readNibble(int quarter_track, uint32_t &bit_position) const
{
  const TrackData *track = getTrackData(quarter_track);
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
    uint8_t bit = readBit(quarter_track, bit_position);
    bit_position = (bit_position + 1) % track->bit_count;
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
