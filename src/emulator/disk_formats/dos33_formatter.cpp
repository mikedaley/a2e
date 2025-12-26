#include "emulator/disk_formats/dos33_formatter.hpp"
#include "emulator/disk_formats/gcr_encoding.hpp"
#include <cstring>

DOS33DiskFormatter::DOS33DiskFormatter(uint8_t volume_number)
    : volume_number_(volume_number)
{
  initializeDiskData();
}

void DOS33DiskFormatter::initializeDiskData()
{
  // Zero all sectors
  for (auto &track : disk_data_)
  {
    for (auto &sector : track)
    {
      sector.fill(0);
    }
  }
}

void DOS33DiskFormatter::buildTrackBitmap(uint8_t *bitmap)
{
  // Track bitmap is 4 bytes per track = 140 bytes total
  // First 2 bytes: bit map of free sectors (16 bits for 16 sectors)
  // Bit = 1 means sector is FREE, bit = 0 means USED
  // Byte order is little-endian: byte 0 = sectors 0-7, byte 1 = sectors 8-15

  std::memset(bitmap, 0, 140);

  for (int track = 0; track < TRACKS; track++)
  {
    int offset = track * 4;

    if (track <= 2 || track == VTOC_TRACK)
    {
      // DOS tracks (0-2) and VTOC/Catalog track (17): all sectors used
      bitmap[offset + 0] = 0x00; // Sectors 0-7 used
      bitmap[offset + 1] = 0x00; // Sectors 8-15 used
    }
    else
    {
      // Data tracks: all sectors free
      bitmap[offset + 0] = 0xFF; // Sectors 0-7 free
      bitmap[offset + 1] = 0xFF; // Sectors 8-15 free
    }
    // Bytes 2-3 are reserved, should be 0
    bitmap[offset + 2] = 0x00;
    bitmap[offset + 3] = 0x00;
  }
}

void DOS33DiskFormatter::buildVTOC()
{
  auto &vtoc = disk_data_[VTOC_TRACK][VTOC_SECTOR];

  // Byte 0x00: Not used
  vtoc[0x00] = 0x00;

  // Byte 0x01: Track number of first catalog sector
  vtoc[0x01] = VTOC_TRACK;

  // Byte 0x02: Sector number of first catalog sector
  vtoc[0x02] = FIRST_CATALOG_SECTOR;

  // Byte 0x03: DOS release number
  vtoc[0x03] = 0x03; // DOS 3.3

  // Bytes 0x04-0x05: Not used
  vtoc[0x04] = 0x00;
  vtoc[0x05] = 0x00;

  // Byte 0x06: Diskette volume number
  vtoc[0x06] = volume_number_;

  // Bytes 0x07-0x26: Not used (reserved)
  // Already zeroed

  // Byte 0x27: Maximum number of track/sector pairs per file
  // (122 pairs = 30.5KB max file size with DOS 3.3)
  vtoc[0x27] = 0x7A;

  // Bytes 0x28-0x2F: Not used
  // Already zeroed

  // Byte 0x30: Last track where sectors were allocated
  vtoc[0x30] = 0x12; // Track 18 (starts allocation from here)

  // Byte 0x31: Direction of track allocation (+1 or -1)
  vtoc[0x31] = 0x01; // +1 = outward

  // Bytes 0x32-0x33: Not used
  // Already zeroed

  // Byte 0x34: Number of tracks per disk
  vtoc[0x34] = TRACKS;

  // Byte 0x35: Number of sectors per track
  vtoc[0x35] = SECTORS_PER_TRACK;

  // Bytes 0x36-0x37: Number of bytes per sector (little-endian)
  vtoc[0x36] = 0x00; // Low byte of 256
  vtoc[0x37] = 0x01; // High byte of 256

  // Bytes 0x38-0xC3: Bit map of free sectors on each track (140 bytes)
  buildTrackBitmap(&vtoc[0x38]);
}

void DOS33DiskFormatter::buildCatalogSector(int sector_num, int next_sector)
{
  auto &catalog = disk_data_[VTOC_TRACK][sector_num];

  // Byte 0x00: Not used
  catalog[0x00] = 0x00;

  // Byte 0x01: Track of next catalog sector (or 0 if last)
  catalog[0x01] = (next_sector > 0) ? VTOC_TRACK : 0x00;

  // Byte 0x02: Sector of next catalog sector (or 0 if last)
  catalog[0x02] = static_cast<uint8_t>(next_sector);

  // Bytes 0x03-0x0A: Not used
  // Already zeroed

  // Bytes 0x0B-0xFF: 7 file descriptive entries (35 bytes each)
  // Each entry:
  //   Byte 0x00: Track of first T/S list sector (0x00=deleted, 0xFF=never used)
  //   Byte 0x01: Sector of first T/S list sector
  //   Byte 0x02: File type and flags
  //   Bytes 0x03-0x20: File name (30 bytes, high bit set, padded with spaces)
  //   Bytes 0x21-0x22: Length of file in sectors (little-endian)

  for (int entry = 0; entry < 7; entry++)
  {
    int offset = 0x0B + (entry * 35);

    // Track of first T/S list = 0xFF means never used
    catalog[offset + 0x00] = 0xFF;

    // Sector of first T/S list
    catalog[offset + 0x01] = 0x00;

    // File type (0x00 = no file)
    catalog[offset + 0x02] = 0x00;

    // File name: 30 bytes of high-bit spaces (0xA0)
    for (int i = 0; i < 30; i++)
    {
      catalog[offset + 0x03 + i] = 0xA0;
    }

    // File length in sectors
    catalog[offset + 0x21] = 0x00;
    catalog[offset + 0x22] = 0x00;
  }
}

void DOS33DiskFormatter::buildCatalog()
{
  // Build catalog sectors from 15 down to 1
  // Each sector links to the next lower sector
  // Sector 1 has next = 0 (end of chain)

  for (int sector = FIRST_CATALOG_SECTOR; sector >= 1; sector--)
  {
    int next = (sector > 1) ? (sector - 1) : 0;
    buildCatalogSector(sector, next);
  }
}

std::vector<uint8_t> DOS33DiskFormatter::nibblesToBits(
    const std::vector<uint8_t> &nibbles)
{
  std::vector<uint8_t> bits;
  bits.reserve((nibbles.size() * 8 + 7) / 8);

  uint8_t current_byte = 0;
  int bit_count = 0;

  for (uint8_t nibble : nibbles)
  {
    // Write 8 bits per nibble, MSB first
    for (int i = 7; i >= 0; i--)
    {
      current_byte = (current_byte << 1) | ((nibble >> i) & 1);
      bit_count++;

      if (bit_count == 8)
      {
        bits.push_back(current_byte);
        current_byte = 0;
        bit_count = 0;
      }
    }
  }

  // Handle remaining bits (pad with zeros)
  if (bit_count > 0)
  {
    current_byte <<= (8 - bit_count);
    bits.push_back(current_byte);
  }

  return bits;
}

std::vector<uint8_t> DOS33DiskFormatter::buildTrackNibbles(int track)
{
  std::vector<uint8_t> track_nibbles;
  track_nibbles.reserve(6656); // Approximate track size

  // Initial gap before first sector (~48 sync bytes)
  for (int i = 0; i < 48; i++)
  {
    track_nibbles.push_back(GCR::SYNC_BYTE);
  }

  // Build each sector in physical order
  for (int phys_sector = 0; phys_sector < SECTORS_PER_TRACK; phys_sector++)
  {
    // Get logical sector for this physical position
    int logical_sector = PHYSICAL_TO_LOGICAL[phys_sector];

    // Build the sector with address and data fields
    auto sector_nibbles = GCR::buildSector(
        volume_number_,
        static_cast<uint8_t>(track),
        static_cast<uint8_t>(logical_sector),
        disk_data_[track][logical_sector].data());

    track_nibbles.insert(track_nibbles.end(),
                         sector_nibbles.begin(),
                         sector_nibbles.end());

    // Gap after sector (~27 sync bytes between sectors)
    // Last sector has larger gap to fill track
    int gap_size = (phys_sector < SECTORS_PER_TRACK - 1) ? 27 : 48;
    for (int i = 0; i < gap_size; i++)
    {
      track_nibbles.push_back(GCR::SYNC_BYTE);
    }
  }

  return track_nibbles;
}

std::vector<std::vector<uint8_t>> DOS33DiskFormatter::generateNibblizedTracks()
{
  // Generate blank tracks filled with sync bytes (0xFF)
  // This represents an unformatted disk that can be INITed
  // Standard track length: ~6400 bytes (51200 bits)

  static constexpr size_t TRACK_NIBBLE_COUNT = 6400;
  static constexpr size_t TRACK_BIT_COUNT = TRACK_NIBBLE_COUNT * 8;

  std::vector<std::vector<uint8_t>> tracks;
  tracks.reserve(TRACKS);
  track_bit_counts_.clear();
  track_bit_counts_.reserve(TRACKS);

  for (int track = 0; track < TRACKS; track++)
  {
    // Create track filled with sync bytes (0xFF)
    // In bit form, this is all 1s
    std::vector<uint8_t> track_bits(TRACK_NIBBLE_COUNT, 0xFF);

    track_bit_counts_.push_back(TRACK_BIT_COUNT);
    tracks.push_back(std::move(track_bits));
  }

  return tracks;
}

std::vector<uint32_t> DOS33DiskFormatter::getTrackBitCounts() const
{
  return track_bit_counts_;
}
