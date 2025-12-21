#include "disk_image.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

// 6-and-2 encoding translate table (64 valid disk nibbles)
// Maps 6-bit values (0-63) to valid disk bytes
const uint8_t DiskImage::TRANS62[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// DOS 3.3 physical to logical sector mapping (from apple2js DO array)
// Maps physical sector (index) to DOS logical sector (value)
const uint8_t DiskImage::DOS_PHYSICAL_TO_LOGICAL[16] = {
    0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
    0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};

DiskImage::DiskImage()
{
  raw_data_.fill(0);
}

bool DiskImage::load(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open())
  {
    std::cerr << "Failed to open disk image: " << filepath << std::endl;
    return false;
  }

  // Check file size
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (size != DSK_IMAGE_SIZE)
  {
    std::cerr << "Invalid disk image size: " << size
              << " (expected " << DSK_IMAGE_SIZE << ")" << std::endl;
    file.close();
    return false;
  }

  // Read raw data
  if (!file.read(reinterpret_cast<char *>(raw_data_.data()), DSK_IMAGE_SIZE))
  {
    std::cerr << "Failed to read disk image data" << std::endl;
    file.close();
    return false;
  }

  file.close();
  filepath_ = filepath;
  loaded_ = true;

  // Pre-nibblize all tracks for fast access during emulation
  nibblizeAllTracks();

  std::cout << "Loaded disk image: " << filepath << std::endl;
  return true;
}

const uint8_t *DiskImage::getSectorData(int track, int sector) const
{
  if (track < 0 || track >= TRACKS || sector < 0 || sector >= SECTORS_PER_TRACK)
  {
    return nullptr;
  }

  int offset = (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR;
  return &raw_data_[offset];
}

uint8_t DiskImage::getNibble(int track, int position) const
{
  if (!loaded_ || track < 0 || track >= TRACKS)
  {
    return 0xFF;
  }

  const auto &nibble_track = nibble_tracks_[track];
  if (nibble_track.empty())
  {
    return 0xFF;
  }

  int pos = position % static_cast<int>(nibble_track.size());
  return nibble_track[pos];
}

void DiskImage::nibblizeAllTracks()
{
  for (int track = 0; track < TRACKS; ++track)
  {
    nibblizeTrack(track);
  }
}

// 4-and-4 encoding helper: encode a byte as two nibbles
static void encode44(std::vector<uint8_t> &buf, uint8_t val)
{
  buf.push_back((val >> 1) | 0xAA);
  buf.push_back(val | 0xAA);
}

void DiskImage::nibblizeTrack(int track)
{
  std::vector<uint8_t> &buf = nibble_tracks_[track];
  buf.clear();
  buf.reserve(NIBBLES_PER_TRACK);

  for (int physical_sector = 0; physical_sector < SECTORS_PER_TRACK; ++physical_sector)
  {
    // Map physical sector to DOS logical sector
    int dos_sector = DOS_PHYSICAL_TO_LOGICAL[physical_sector];
    
    // Get sector data from .dsk file
    const uint8_t *src = getSectorData(track, dos_sector);
    if (!src)
    {
      continue;
    }

    // Gap 1 (first sector) or Gap 3 (between sectors)
    int gap = (physical_sector == 0) ? 48 : 6;
    for (int i = 0; i < gap; ++i)
    {
      buf.push_back(0xFF);
    }

    // === Address Field ===
    // Prologue
    buf.push_back(0xD5);
    buf.push_back(0xAA);
    buf.push_back(0x96);

    // 4-and-4 encoded values
    uint8_t checksum = volume_ ^ track ^ physical_sector;
    encode44(buf, volume_);
    encode44(buf, static_cast<uint8_t>(track));
    encode44(buf, static_cast<uint8_t>(physical_sector));
    encode44(buf, checksum);

    // Epilogue
    buf.push_back(0xDE);
    buf.push_back(0xAA);
    buf.push_back(0xEB);

    // Gap 2: 6 bytes
    for (int i = 0; i < 6; ++i)
    {
      buf.push_back(0xFF);
    }

    // === Data Field ===
    // Prologue
    buf.push_back(0xD5);
    buf.push_back(0xAA);
    buf.push_back(0xAD);

    // 6-and-2 encode the sector data (apple2ts algorithm)
    // This produces 343 nibbles (342 data + 1 checksum)
    uint8_t dest[343];
    
    // Bit reverse table for 2-bit values
    static const uint8_t bit_reverse[4] = {0, 2, 1, 3};
    
    // Build auxiliary bytes (first 86 bytes contain 2-bit fragments)
    // Each aux byte holds 2-bit pieces from 3 source bytes (or 2 for last two)
    for (int c = 0; c < 84; ++c)
    {
      dest[c] = bit_reverse[src[c] & 3] | 
                (bit_reverse[src[c + 86] & 3] << 2) | 
                (bit_reverse[src[c + 172] & 3] << 4);
    }
    dest[84] = bit_reverse[src[84] & 3] | (bit_reverse[src[170] & 3] << 2);
    dest[85] = bit_reverse[src[85] & 3] | (bit_reverse[src[171] & 3] << 2);
    
    // Build main data bytes (bytes 86-341 contain upper 6 bits)
    for (int c = 0; c < 256; ++c)
    {
      dest[86 + c] = src[c] >> 2;
    }
    
    // Set checksum byte initially
    dest[342] = dest[341];
    
    // XOR chain encoding (work backwards)
    for (int location = 341; location > 0; --location)
    {
      dest[location] ^= dest[location - 1];
    }
    
    // Translate through 6-and-2 table and output
    for (int c = 0; c < 343; ++c)
    {
      buf.push_back(TRANS62[dest[c] & 0x3F]);
    }

    // Epilogue
    buf.push_back(0xDE);
    buf.push_back(0xAA);
    buf.push_back(0xEB);
  }

  // Pad to standard track size with sync bytes
  while (buf.size() < NIBBLES_PER_TRACK)
  {
    buf.push_back(0xFF);
  }
  if (buf.size() > NIBBLES_PER_TRACK)
  {
    buf.resize(NIBBLES_PER_TRACK);
  }
}
