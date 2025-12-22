#include "emulator/disk_image.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>

// Static member initialization
uint8_t DiskImage::nibble_decode_[256];
bool DiskImage::decode_table_initialized_ = false;

// Static constexpr member definitions
constexpr int DiskImage::DOS33_SECTOR_MAP[16];
constexpr int DiskImage::PRODOS_SECTOR_MAP[16];
constexpr uint8_t DiskImage::NIBBLE_ENCODE[64];

DiskImage::DiskImage()
{
  initializeDecodeTable();
  data_.fill(0x00);
}

void DiskImage::initializeDecodeTable()
{
  if (decode_table_initialized_) return;

  // Initialize all entries as invalid
  std::fill(std::begin(nibble_decode_), std::end(nibble_decode_), 0xFF);

  // Build reverse lookup from encode table
  for (int i = 0; i < 64; i++)
  {
    nibble_decode_[NIBBLE_ENCODE[i]] = static_cast<uint8_t>(i);
  }

  decode_table_initialized_ = true;
}

bool DiskImage::load(const std::string& filepath)
{
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file)
  {
    return false;
  }

  // Check file size
  std::streamsize size = file.tellg();
  if (size != DISK_SIZE)
  {
    return false;  // Not a valid DSK file
  }

  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(data_.data()), DISK_SIZE))
  {
    return false;
  }

  filepath_ = filepath;
  format_ = detectFormat(filepath);
  loaded_ = true;
  modified_ = false;

  return true;
}

bool DiskImage::save(const std::string& filepath)
{
  std::ofstream file(filepath, std::ios::binary);
  if (!file)
  {
    return false;
  }

  if (!file.write(reinterpret_cast<const char*>(data_.data()), DISK_SIZE))
  {
    return false;
  }

  filepath_ = filepath;
  modified_ = false;
  return true;
}

bool DiskImage::save()
{
  if (filepath_.empty())
  {
    return false;
  }
  return save(filepath_);
}

DiskFormat DiskImage::detectFormat(const std::string& filepath) const
{
  // Get file extension
  size_t dot = filepath.rfind('.');
  if (dot == std::string::npos)
  {
    return DiskFormat::DOS33;  // Default
  }

  std::string ext = filepath.substr(dot);
  // Convert to lowercase
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".po")
  {
    return DiskFormat::PRODOS;
  }

  // .dsk, .do, and others default to DOS 3.3
  return DiskFormat::DOS33;
}

size_t DiskImage::getSectorOffset(int track, int logicalSector) const
{
  if (track < 0 || track >= TRACKS || logicalSector < 0 || logicalSector >= SECTORS_PER_TRACK)
  {
    return 0;
  }

  // .dsk files store sectors in logical order
  // Offset = (track * sectors_per_track + logical_sector) * bytes_per_sector
  // No mapping needed - the sector parameter is already a logical sector number
  return (track * SECTORS_PER_TRACK + logicalSector) * BYTES_PER_SECTOR;
}

const uint8_t* DiskImage::getSector(int track, int sector) const
{
  if (track < 0 || track >= TRACKS || sector < 0 || sector >= SECTORS_PER_TRACK)
  {
    return nullptr;
  }

  size_t offset = getSectorOffset(track, sector);
  return &data_[offset];
}

uint8_t* DiskImage::getSectorMutable(int track, int sector)
{
  if (track < 0 || track >= TRACKS || sector < 0 || sector >= SECTORS_PER_TRACK)
  {
    return nullptr;
  }

  modified_ = true;
  size_t offset = getSectorOffset(track, sector);
  return &data_[offset];
}

void DiskImage::format()
{
  data_.fill(0x00);
  modified_ = true;
}

void DiskImage::encode4and4(uint8_t value, uint8_t* dest) const
{
  // Encode one byte as two disk bytes using 4-and-4 encoding
  // First byte: 1 b7 1 b5 1 b3 1 b1
  // Second byte: 1 b6 1 b4 1 b2 1 b0
  dest[0] = 0xAA | ((value >> 1) & 0x55);
  dest[1] = 0xAA | (value & 0x55);
}

uint8_t DiskImage::decode4and4(const uint8_t* src) const
{
  // Decode two 4-and-4 encoded bytes back to original value
  return ((src[0] << 1) | 0x01) & src[1];
}

void DiskImage::encode6and2(const uint8_t* src, uint8_t* dest) const
{
  // 6-and-2 encoding: 256 bytes -> 343 disk bytes (342 data + 1 checksum)
  // 
  // The 256 source bytes are split into:
  // - auxBuf[0..85]: Low 2 bits from each byte (with bit reversal)
  // - dataBuf[0..255]: High 6 bits from each byte
  //
  // The auxiliary buffer packs 2-bit fragments from 3 bytes into each 6-bit value:
  //   auxBuf[i] bits 1,0 <- data[i] bits 1,0 (reversed)
  //   auxBuf[i] bits 3,2 <- data[i+86] bits 1,0 (reversed)
  //   auxBuf[i] bits 5,4 <- data[i+172] bits 1,0 (reversed)

  uint8_t auxBuf[86];
  uint8_t dataBuf[256];

  // Pre-nibblize: extract low 2 bits to aux, high 6 bits to data
  for (int i = 0; i < 86; i++)
  {
    uint8_t aux = 0;
    // Reverse bits within each 2-bit group (bit 0 -> position 1, bit 1 -> position 0)
    aux = ((src[i] & 0x01) << 1) | ((src[i] & 0x02) >> 1);
    if (i + 86 < 256)
      aux |= ((src[i + 86] & 0x01) << 3) | ((src[i + 86] & 0x02) << 1);
    if (i + 172 < 256)
      aux |= ((src[i + 172] & 0x01) << 5) | ((src[i + 172] & 0x02) << 3);
    auxBuf[i] = aux;
  }

  for (int i = 0; i < 256; i++)
  {
    dataBuf[i] = src[i] >> 2;
  }

  // XOR encode: each value is XORed with the previous original value
  uint8_t prev = 0;
  for (int i = 0; i < 86; i++)
  {
    dest[i] = NIBBLE_ENCODE[auxBuf[i] ^ prev];
    prev = auxBuf[i];
  }
  for (int i = 0; i < 256; i++)
  {
    dest[86 + i] = NIBBLE_ENCODE[dataBuf[i] ^ prev];
    prev = dataBuf[i];
  }
  dest[342] = NIBBLE_ENCODE[prev];  // Checksum
}

bool DiskImage::decode6and2(const uint8_t* src, uint8_t* dest) const
{
  // 6-and-2 decoding: 343 disk bytes -> 256 bytes
  // Reverse of encode6and2

  uint8_t auxBuf[86];
  uint8_t dataBuf[256];

  // XOR decode: each decoded value is XORed with the previous DECODED value
  // This is the critical fix - prev must track the decoded result, not the raw nibble
  uint8_t prev = 0;
  for (int i = 0; i < 86; i++)
  {
    uint8_t val = nibble_decode_[src[i]];
    if (val == 0xFF)
    {
      return false;  // Invalid nibble
    }
    auxBuf[i] = val ^ prev;
    prev = auxBuf[i];  // Use decoded result as prev for next iteration
  }
  for (int i = 0; i < 256; i++)
  {
    uint8_t val = nibble_decode_[src[86 + i]];
    if (val == 0xFF)
    {
      return false;  // Invalid nibble
    }
    dataBuf[i] = val ^ prev;
    prev = dataBuf[i];  // Use decoded result as prev
  }

  // Verify checksum (should XOR to 0 with prev)
  uint8_t checksum = nibble_decode_[src[342]];
  if ((checksum ^ prev) != 0)
  {
    return false;  // Checksum mismatch
  }

  // Reconstruct bytes from auxBuf and dataBuf
  for (int i = 0; i < 256; i++)
  {
    // High 6 bits from dataBuf
    uint8_t byte = dataBuf[i] << 2;

    // Low 2 bits from auxBuf (with bit reversal to undo encoding)
    uint8_t aux = auxBuf[i % 86];

    if (i < 86)
    {
      // Bits 1,0 of aux -> bits 1,0 of byte (reversed)
      byte |= ((aux & 0x02) >> 1) | ((aux & 0x01) << 1);
    }
    else if (i < 172)
    {
      // Bits 3,2 of aux -> bits 1,0 of byte (reversed)
      byte |= ((aux & 0x08) >> 3) | ((aux & 0x04) >> 1);
    }
    else
    {
      // Bits 5,4 of aux -> bits 1,0 of byte (reversed)
      byte |= ((aux & 0x20) >> 5) | ((aux & 0x10) >> 3);
    }

    dest[i] = byte;
  }

  return true;
}

std::vector<uint8_t> DiskImage::nibblizeSector(int track, int sector, uint8_t volume) const
{
  std::vector<uint8_t> nibbles;
  nibbles.reserve(400);

  const uint8_t* sectorData = getSector(track, sector);
  if (!sectorData)
  {
    return nibbles;
  }

  // Gap 2 - sync bytes before address field
  for (int i = 0; i < 6; i++)
  {
    nibbles.push_back(0xFF);
  }

  // Address field prologue
  nibbles.push_back(0xD5);
  nibbles.push_back(0xAA);
  nibbles.push_back(0x96);

  // Volume (4-and-4 encoded)
  uint8_t encoded[2];
  encode4and4(volume, encoded);
  nibbles.push_back(encoded[0]);
  nibbles.push_back(encoded[1]);

  // Track (4-and-4 encoded)
  encode4and4(static_cast<uint8_t>(track), encoded);
  nibbles.push_back(encoded[0]);
  nibbles.push_back(encoded[1]);

  // Sector (4-and-4 encoded)
  encode4and4(static_cast<uint8_t>(sector), encoded);
  nibbles.push_back(encoded[0]);
  nibbles.push_back(encoded[1]);

  // Checksum (XOR of volume, track, sector)
  encode4and4(volume ^ track ^ sector, encoded);
  nibbles.push_back(encoded[0]);
  nibbles.push_back(encoded[1]);

  // Address field epilogue
  nibbles.push_back(0xDE);
  nibbles.push_back(0xAA);
  nibbles.push_back(0xEB);

  // Gap 2 - sync bytes before data field
  for (int i = 0; i < 6; i++)
  {
    nibbles.push_back(0xFF);
  }

  // Data field prologue
  nibbles.push_back(0xD5);
  nibbles.push_back(0xAA);
  nibbles.push_back(0xAD);

  // Encode sector data (6-and-2)
  uint8_t encodedData[343];
  encode6and2(sectorData, encodedData);
  for (int i = 0; i < 343; i++)
  {
    nibbles.push_back(encodedData[i]);
  }

  // Data field epilogue
  nibbles.push_back(0xDE);
  nibbles.push_back(0xAA);
  nibbles.push_back(0xEB);

  return nibbles;
}

std::vector<uint8_t> DiskImage::getNibblizedTrack(int track, uint8_t volume) const
{
  if (track < 0 || track >= TRACKS)
  {
    return {};
  }

  std::cerr << "\n*** getNibblizedTrack(track=" << track << ", volume=" << (int)volume << ") ***\n";

  std::vector<uint8_t> trackData;
  trackData.reserve(NIBBLES_PER_TRACK);

  // Gap 1 - initial sync bytes at start of track
  for (int i = 0; i < 48; i++)
  {
    trackData.push_back(0xFF);
  }

  // Nibblize all 16 sectors
  // NOTE: Apply DOS 3.3 sector interleave mapping (physical to logical)
  // The DSK file stores sectors in logical order, but the nibblized track uses physical order
  for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
  {
    // Map physical sector position to logical sector number
    int logicalSector = DOS33_SECTOR_MAP[sector];
    auto sectorNibbles = nibblizeSector(track, logicalSector, volume);
    trackData.insert(trackData.end(), sectorNibbles.begin(), sectorNibbles.end());
  }

  // Pad to standard track size if needed
  while (trackData.size() < NIBBLES_PER_TRACK)
  {
    trackData.push_back(0xFF);
  }

  // Trim if over (shouldn't happen with standard format)
  if (trackData.size() > NIBBLES_PER_TRACK)
  {
    trackData.resize(NIBBLES_PER_TRACK);
  }

  return trackData;
}

bool DiskImage::decodeTrack(int track, const std::vector<uint8_t>& nibbleData)
{
  if (track < 0 || track >= TRACKS)
  {
    return false;
  }

  size_t pos = 0;
  int sectorsDecoded = 0;

  while (pos < nibbleData.size() - 400 && sectorsDecoded < SECTORS_PER_TRACK)
  {
    // Look for address field prologue: D5 AA 96
    if (nibbleData[pos] == 0xD5 && 
        pos + 1 < nibbleData.size() && nibbleData[pos + 1] == 0xAA &&
        pos + 2 < nibbleData.size() && nibbleData[pos + 2] == 0x96)
    {
      pos += 3;

      // Decode address field
      if (pos + 8 > nibbleData.size()) break;

      uint8_t vol = decode4and4(&nibbleData[pos]);
      pos += 2;
      uint8_t trk = decode4and4(&nibbleData[pos]);
      pos += 2;
      uint8_t sec = decode4and4(&nibbleData[pos]);
      pos += 2;
      uint8_t chk = decode4and4(&nibbleData[pos]);
      pos += 2;

      // Verify checksum
      if (chk != (vol ^ trk ^ sec))
      {
        continue;
      }

      // Verify track number
      if (trk != track)
      {
        continue;
      }

      // Skip to data field prologue: D5 AA AD
      bool foundData = false;
      for (int i = 0; i < 50 && pos + 2 < nibbleData.size(); i++, pos++)
      {
        if (nibbleData[pos] == 0xD5 && 
            nibbleData[pos + 1] == 0xAA &&
            nibbleData[pos + 2] == 0xAD)
        {
          pos += 3;
          foundData = true;
          break;
        }
      }

      if (!foundData || pos + 343 > nibbleData.size())
      {
        continue;
      }

      // Decode sector data
      uint8_t* sectorDest = getSectorMutable(track, sec);
      if (sectorDest && decode6and2(&nibbleData[pos], sectorDest))
      {
        sectorsDecoded++;
      }
      pos += 343;
    }
    else
    {
      pos++;
    }
  }

  return sectorsDecoded == SECTORS_PER_TRACK;
}
