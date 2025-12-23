#include "emulator/disk_image.hpp"
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
    const uint8_t *data = getSectorData(track, dos_sector);
    if (!data)
    {
      continue;
    }

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
    encode44(buf, track);
    encode44(buf, physical_sector);
    encode44(buf, checksum);

    // Epilogue
    buf.push_back(0xDE);
    buf.push_back(0xAA);
    buf.push_back(0xEB);

    // Gap 2: 5 bytes
    for (int i = 0; i < 5; ++i)
    {
      buf.push_back(0xFF);
    }

    // === Data Field ===
    // Prologue
    buf.push_back(0xD5);
    buf.push_back(0xAA);
    buf.push_back(0xAD);

    // 6-and-2 encode the sector data (apple2js algorithm)
    // This produces 342 nibbles + 1 checksum = 343 total
    uint8_t nibbles[0x156]; // 342 bytes
    memset(nibbles, 0, sizeof(nibbles));

    const int ptr2 = 0;      // Auxiliary nibbles at offset 0
    const int ptr6 = 0x56;   // Data nibbles at offset 86

    // Process 257 bytes (256 data + wrap) into auxiliary and data nibbles
    int idx2 = 0x55; // Start at 85, count down
    for (int idx6 = 0x101; idx6 >= 0; --idx6)
    {
      uint8_t val6 = data[idx6 % 0x100];
      uint8_t val2 = nibbles[ptr2 + idx2];

      // Extract low 2 bits into auxiliary nibble
      val2 = (val2 << 1) | (val6 & 1);
      val6 >>= 1;
      val2 = (val2 << 1) | (val6 & 1);
      val6 >>= 1;

      // Store high 6 bits in data nibble, low 2 bits accumulated in aux
      nibbles[ptr6 + idx6] = val6;
      nibbles[ptr2 + idx2] = val2;

      if (--idx2 < 0)
      {
        idx2 = 0x55;
      }
    }

    // Encode nibbles with XOR chaining and TRANS62 lookup
    uint8_t last = 0;
    for (int i = 0; i < 0x156; ++i)
    {
      uint8_t val = nibbles[i];
      buf.push_back(TRANS62[last ^ val]);
      last = val;
    }
    buf.push_back(TRANS62[last]); // Final checksum nibble

    // Epilogue
    buf.push_back(0xDE);
    buf.push_back(0xAA);
    buf.push_back(0xEB);

    // Gap 3 end: 1 byte
    buf.push_back(0xFF);
  }

  // Pad or truncate to standard track size
  while (buf.size() < NIBBLES_PER_TRACK)
  {
    buf.push_back(0xFF);
  }
  if (buf.size() > NIBBLES_PER_TRACK)
  {
    buf.resize(NIBBLES_PER_TRACK);
  }
}
