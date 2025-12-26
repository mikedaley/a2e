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

// DOS 3.3 physical to logical sector mapping (for .do files only)
// .dsk files store sectors in physical order and don't need this mapping
// .do files store sectors in DOS logical order and need this mapping
const uint8_t DiskImage::DOS_PHYSICAL_TO_LOGICAL[16] = {
    0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
    0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};

DiskImage::DiskImage() : rng_(std::random_device{}())
{
  raw_data_.fill(0);
  track_map_.fill(0xFF); // 0xFF = empty track
  woz2_bit_positions_.fill(0); // Initialize bit positions to 0
  woz2_last_position_.fill(-1); // Initialize to -1 to detect first read
}

bool DiskImage::load(const std::string &filepath)
{
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open())
  {
    std::cerr << "Failed to open disk image: " << filepath << std::endl;
    return false;
  }

  // Get file size and read entire file
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> file_data(size);
  if (!file.read(reinterpret_cast<char *>(file_data.data()), size))
  {
    std::cerr << "Failed to read disk image data" << std::endl;
    file.close();
    return false;
  }
  file.close();

  // Detect format based on header/size
  bool success = false;

  // Check for WOZ2 signature: "WOZ2" (0x325A4F57 little-endian)
  if (size >= 12 &&
      file_data[0] == 'W' && file_data[1] == 'O' &&
      file_data[2] == 'Z' && file_data[3] == '2')
  {
    std::cout << "Detected WOZ2 format" << std::endl;
    success = loadWOZ2(file_data);
    if (success)
    {
      format_ = Format::WOZ2;
    }
  }
  // Check for DSK size (143,360 bytes)
  else if (size == DSK_IMAGE_SIZE)
  {
    std::cout << "Detected DSK format" << std::endl;
    success = loadDSK(file_data);
    if (success)
    {
      format_ = Format::DSK;
    }
  }
  else
  {
    std::cerr << "Unknown disk image format (size: " << size << " bytes)" << std::endl;
    return false;
  }

  if (success)
  {
    filepath_ = filepath;
    loaded_ = true;
    std::cout << "Loaded disk image: " << filepath << std::endl;
  }

  return success;
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

  // Handle based on format
  if (format_ == Format::DSK)
  {
    const auto &nibble_track = nibble_tracks_[track];
    if (nibble_track.empty())
    {
      static bool logged_empty = false;
      if (!logged_empty) {
        std::cout << "[DISK_IMAGE] WARNING: nibble_track[" << track << "] is EMPTY!" << std::endl;
        logged_empty = true;
      }
      return 0xFF;
    }

    int pos = position % static_cast<int>(nibble_track.size());
    return nibble_track[pos];
  }
  else if (format_ == Format::WOZ2)
  {
    // WOZ2 stores raw bitstream - implement self-sync reading like real Disk II
    // Per WOZ2 spec: read bits until we find a valid self-sync nibble (high bit set)

    // Map whole track to quarter-track index
    int quarter_track = track * 4;
    if (quarter_track >= WOZ2_MAX_TRACKS)
    {
      return 0xFF;
    }

    uint8_t tmap_index = track_map_[quarter_track];
    if (tmap_index == 0xFF || tmap_index >= WOZ2_MAX_TRACKS)
    {
      return 0xFF;
    }

    const WOZ2Track &woz_track = woz2_tracks_[tmap_index];
    if (woz_track.data.empty() || woz_track.bit_count == 0)
    {
      return 0xFF;
    }

    // Check if position has advanced - only read new nibble if position changed
    int &last_pos = woz2_last_position_[tmap_index];
    int &bit_pos = woz2_bit_positions_[tmap_index];

    // If position hasn't changed, we're re-reading the same nibble (shouldn't happen much)
    // If position jumped backward, reset (track change)
    if (position < last_pos && last_pos != -1)
    {
      // Position wrapped or track changed - reset bit position
      bit_pos = 0;
    }

    // Only advance and read new nibble if position incremented
    if (position != last_pos)
    {
      last_pos = position;

      // Read bits and look for a self-sync nibble (byte with high bit set)
      // The MC3470 shifts in bits until the shift register has bit 7 set
      uint8_t shift_reg = 0;
      int bits_read = 0;
      const int MAX_BITS_TO_READ = 1000; // Prevent infinite loop

      while (bits_read < MAX_BITS_TO_READ)
      {
        // Read one bit from bitstream
        int byte_offset = bit_pos / 8;
        int bit_offset = 7 - (bit_pos % 8); // MSB first

        if (byte_offset >= static_cast<int>(woz_track.data.size()))
        {
          bit_pos = 0; // Wrap around
          byte_offset = 0;
          bit_offset = 7;
        }

        uint8_t bit = (woz_track.data[byte_offset] >> bit_offset) & 1;

        // Shift bit into register
        shift_reg = (shift_reg << 1) | bit;

        // Advance bit position
        bit_pos = (bit_pos + 1) % woz_track.bit_count;
        bits_read++;

        // Check if we have a valid nibble (high bit set)
        if (shift_reg & 0x80)
        {
          // Found a valid self-sync nibble - cache it
          static thread_local std::array<uint8_t, WOZ2_MAX_TRACKS> cached_nibble;
          cached_nibble[tmap_index] = shift_reg;

          // Debug: Look for address field prologue (D5 AA 96)
          static uint8_t last_two[2] = {0, 0};
          if (track == 0 && last_two[0] == 0xD5 && last_two[1] == 0xAA && shift_reg == 0x96)
          {
            static int prologue_count = 0;
            if (prologue_count < 5)
            {
              std::cout << "Found address field prologue D5 AA 96 on track 0" << std::endl;
              prologue_count++;
            }
          }
          last_two[0] = last_two[1];
          last_two[1] = shift_reg;

          return shift_reg;
        }
      }

      // Shouldn't happen, but return 0xFF if we don't find a valid nibble
      return 0xFF;
    }
    else
    {
      // Re-reading same position - return cached nibble
      static thread_local std::array<uint8_t, WOZ2_MAX_TRACKS> cached_nibble;
      return cached_nibble[tmap_index];
    }
  }

  return 0xFF;
}

int DiskImage::getNibbleTrackSize(int track) const
{
  if (!loaded_)
  {
    return 0;
  }

  if (format_ == Format::DSK)
  {
    // Validate track bounds for DSK format
    if (track < 0 || track >= TRACKS)
    {
      return 0;
    }
    return NIBBLES_PER_TRACK;
  }
  else if (format_ == Format::WOZ2)
  {
    // Validate track bounds
    if (track < 0 || track >= TRACKS)
    {
      return 0;
    }
    // Map whole track to quarter-track index (track * 4)
    int quarter_track = track * 4;
    if (quarter_track >= WOZ2_MAX_TRACKS)
    {
      return 0;
    }

    uint8_t tmap_index = track_map_[quarter_track];
    if (tmap_index == 0xFF)
    {
      return 0; // Empty track
    }

    if (tmap_index >= WOZ2_MAX_TRACKS)
    {
      return 0;
    }

    // Return size in bytes (bit_count / 8)
    return woz2_tracks_[tmap_index].bit_count / 8;
  }

  return 0;
}

uint8_t DiskImage::getNibbleAtQuarterTrack(int quarter_track, int position) const
{
  if (!loaded_)
  {
    return 0xFF;
  }

  // For DSK format, quarter-tracks map to whole tracks (no quarter-track data available)
  // Round to nearest whole track: quarter_track 0-3 -> track 0, 4-7 -> track 1, etc.
  if (format_ == Format::DSK)
  {
    int track = quarter_track / 4;
    return getNibble(track, position);
  }
  else if (format_ == Format::WOZ2)
  {
    // WOZ2 has proper quarter-track support via TMAP
    // For now, just use whole track (WOZ2 support to be added later)
    int track = quarter_track / 4;
    return getNibble(track, position);
  }

  return 0xFF;
}

int DiskImage::getNibbleTrackSizeAtQuarterTrack(int quarter_track) const
{
  if (!loaded_)
  {
    return 0;
  }

  // Validate quarter-track bounds (0-139 for 35 tracks)
  if (quarter_track < 0 || quarter_track >= TRACKS * 4)
  {
    return 0;
  }

  // For DSK format, use the whole track size
  if (format_ == Format::DSK)
  {
    return NIBBLES_PER_TRACK;
  }
  else if (format_ == Format::WOZ2)
  {
    // WOZ2 has proper quarter-track support via TMAP
    // For now, just use whole track size (WOZ2 support to be added later)
    int track = quarter_track / 4;
    return getNibbleTrackSize(track);
  }

  return 0;
}

void DiskImage::nibblizeAllTracks()
{
  for (int track = 0; track < TRACKS; ++track)
  {
    nibblizeTrack(track);
  }

  // Debug: dump track 0 nibbles to file for inspection
  if (!nibble_tracks_[0].empty())
  {
    std::ofstream dump("/tmp/track0_nibbles.txt");
    const auto& track = nibble_tracks_[0];

    dump << "Track 0 nibble dump (" << track.size() << " bytes total)\n\n";

    // Look for all D5 AA 96 prologues and log their positions
    for (size_t i = 0; i + 2 < track.size(); ++i)
    {
      if (track[i] == 0xD5 && track[i+1] == 0xAA && track[i+2] == 0x96)
      {
        dump << "Address field prologue at position " << i << "\n";
        // Dump surrounding bytes
        dump << "  Context: ";
        for (size_t j = (i >= 10 ? i - 10 : 0); j < i + 20 && j < track.size(); ++j)
        {
          if (j == i) dump << "[";
          dump << std::hex << std::setw(2) << std::setfill('0') << (int)track[j] << " ";
          if (j == i + 2) dump << "] ";
        }
        dump << std::dec << "\n";
      }
    }

    // Dump specific position 4820 and surrounding area
    dump << "\nPosition 4820 and surroundings:\n";
    for (int i = 4810; i < 4850 && i < (int)track.size(); ++i)
    {
      if (i == 4820) dump << "[";
      dump << std::hex << std::setw(2) << std::setfill('0') << (int)track[i] << " ";
      if (i == 4820) dump << "] ";
      if ((i - 4810) % 16 == 15) dump << "\n";
    }
    dump << std::dec << "\n";

    dump.close();
    std::cout << "Dumped track 0 nibbles to /tmp/track0_nibbles.txt" << std::endl;
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
    // Map physical sector to DOS logical sector using DOS 3.3 interleave
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
    // Address field contains PHYSICAL sector number (what the Disk II ROM searches for)
    // The data field contains data for the LOGICAL sector (from the .dsk file)
    // The BOOT1 interleave table maps logical->physical, so when BOOT1 wants logical
    // sector N, it looks up physical sector P = interleave_tab[N] and tells the ROM
    // to find address field with sector=P
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

    // 6-and-2 encode the sector data
    // Standard Apple II 6-and-2 GCR encoding:
    // - 256 data bytes → 342 nibbles (86 aux + 256 data) + 1 checksum
    // - Each byte split: high 6 bits → data nibble, low 2 bits → aux nibble
    // The auxiliary buffer packs 2-bit fragments from 3 bytes into each 6-bit value:
    //   auxBuf[i] bits 1,0 <- data[i] bits 1,0 (reversed)
    //   auxBuf[i] bits 3,2 <- data[i+86] bits 1,0 (reversed)
    //   auxBuf[i] bits 5,4 <- data[i+172] bits 1,0 (reversed)

    uint8_t auxBuf[86];
    uint8_t dataBuf[256];

    // Pre-nibblize: extract low 2 bits to aux, high 6 bits to data
    // Apple II 6-and-2 encoding packs low 2 bits from 3 bytes into each aux byte.
    // The Disk II ROM decode reverses bits within each 2-bit group, so we must
    // PRE-reverse them here so they come out correct after the ROM's reversal.
    for (int i = 0; i < 86; i++)
    {
      uint8_t aux = 0;
      // Pre-reverse bits: data bits 0,1 -> aux bits 1,0
      aux = ((data[i] & 0x01) << 1) | ((data[i] & 0x02) >> 1);
      if (i + 86 < 256)
        aux |= ((data[i + 86] & 0x01) << 3) | ((data[i + 86] & 0x02) << 1);
      if (i + 172 < 256)
        aux |= ((data[i + 172] & 0x01) << 5) | ((data[i + 172] & 0x02) << 3);
      auxBuf[i] = aux;
    }

    for (int i = 0; i < 256; i++)
    {
      dataBuf[i] = data[i] >> 2;
    }

    // XOR encode: each value is XORed with the previous original value
    uint8_t prev = 0;
    for (int i = 0; i < 86; i++)
    {
      buf.push_back(TRANS62[auxBuf[i] ^ prev]);
      prev = auxBuf[i];
    }
    for (int i = 0; i < 256; i++)
    {
      buf.push_back(TRANS62[dataBuf[i] ^ prev]);
      prev = dataBuf[i];
    }
    buf.push_back(TRANS62[prev]); // Checksum

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

int DiskImage::findSectorAtPosition(int track, int position) const
{
  if (!loaded_ || track < 0 || track >= TRACKS)
  {
    return -1;
  }

  const auto &nibble_track = nibble_tracks_[track];
  if (nibble_track.empty())
  {
    return -1;
  }

  int track_size = static_cast<int>(nibble_track.size());
  position = position % track_size;

  // Scan backwards up to a full track to find address field prologue (D5 AA 96)
  for (int i = 0; i < track_size; ++i)
  {
    int pos = (position - i + track_size) % track_size;

    // Check for address field prologue
    int next1 = (pos + 1) % track_size;
    int next2 = (pos + 2) % track_size;

    if (nibble_track[pos] == 0xD5 &&
        nibble_track[next1] == 0xAA &&
        nibble_track[next2] == 0x96)
    {
      // Found address field prologue
      // Skip volume (2 nibbles) and track (2 nibbles), get to sector
      int sector_pos = (pos + 3 + 2 + 2) % track_size;
      int sector_pos2 = (sector_pos + 1) % track_size;

      // Decode 4-and-4 encoded sector number
      uint8_t sector_hi = nibble_track[sector_pos];
      uint8_t sector_lo = nibble_track[sector_pos2];

      // 4-and-4 decode: combine odd and even bits
      int sector = ((sector_hi << 1) | 0x01) & sector_lo;

      if (sector >= 0 && sector < SECTORS_PER_TRACK)
      {
        return sector;
      }
    }
  }

  return -1; // No valid sector found
}

// ============================================================================
// DSK Format Implementation
// ============================================================================

bool DiskImage::loadDSK(const std::vector<uint8_t> &data)
{
  if (data.size() != DSK_IMAGE_SIZE)
  {
    std::cerr << "Invalid DSK size: " << data.size() << std::endl;
    return false;
  }

  // Copy raw data
  std::copy(data.begin(), data.end(), raw_data_.begin());

  // Pre-nibblize all tracks for fast access during emulation
  nibblizeAllTracks();

  return true;
}

// ============================================================================
// WOZ2 Format Implementation
// ============================================================================

// CRC32 table for Gary S. Brown's algorithm
static const uint32_t CRC32_TABLE[256] = {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
  0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
  0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
  0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
  0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
  0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
  0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
  0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
  0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
  0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
  0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
  0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
  0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
  0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
  0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
  0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
  0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
  0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
  0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
  0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
  0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
  0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
  0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
  0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
  0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
  0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
  0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t DiskImage::calculateCRC32(const uint8_t *data, size_t length) const
{
  // WOZ2 uses standard CRC32 with initial value 0x00000000 (no pre/post XOR)
  uint32_t crc = 0x00000000;
  for (size_t i = 0; i < length; i++)
  {
    crc = CRC32_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc;
}

bool DiskImage::loadWOZ2(const std::vector<uint8_t> &data)
{
  if (data.size() < 12)
  {
    std::cerr << "WOZ2 file too small" << std::endl;
    return false;
  }

  // Verify header
  if (data[0] != 'W' || data[1] != 'O' || data[2] != 'Z' || data[3] != '2')
  {
    std::cerr << "Invalid WOZ2 signature" << std::endl;
    return false;
  }

  if (data[4] != 0xFF)
  {
    std::cerr << "Invalid WOZ2 validation marker" << std::endl;
    return false;
  }

  // Check CRC32 (bytes 8-11, little-endian)
  uint32_t stored_crc = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
  if (stored_crc != 0)
  {
    uint32_t calculated_crc = calculateCRC32(&data[12], data.size() - 12);
    if (calculated_crc != stored_crc)
    {
      std::cerr << "Warning: WOZ2 CRC32 mismatch (stored: 0x" << std::hex << stored_crc
                << ", calculated: 0x" << calculated_crc << std::dec << ")" << std::endl;
      std::cerr << "Continuing to load anyway..." << std::endl;
      // Don't fail - continue loading to test the parser
    }
    else
    {
      std::cout << "WOZ2 CRC32 validation passed" << std::endl;
    }
  }

  std::cout << "WOZ2 header validated" << std::endl;

  // Parse chunks starting at byte 12
  size_t offset = 12;
  bool has_info = false;
  bool has_tmap = false;
  bool has_trks = false;

  while (offset + 8 <= data.size())
  {
    // Read chunk header (8 bytes)
    uint32_t chunk_id = data[offset] | (data[offset + 1] << 8) |
                        (data[offset + 2] << 16) | (data[offset + 3] << 24);
    uint32_t chunk_size = data[offset + 4] | (data[offset + 5] << 8) |
                          (data[offset + 6] << 16) | (data[offset + 7] << 24);

    offset += 8;

    if (offset + chunk_size > data.size())
    {
      std::cerr << "Chunk extends beyond file size" << std::endl;
      break;
    }

    // Convert chunk ID to string for display
    char chunk_name[5] = {
        static_cast<char>(chunk_id & 0xFF),
        static_cast<char>((chunk_id >> 8) & 0xFF),
        static_cast<char>((chunk_id >> 16) & 0xFF),
        static_cast<char>((chunk_id >> 24) & 0xFF),
        '\0'};

    std::cout << "Found chunk: " << chunk_name << " (size: " << chunk_size << " bytes)" << std::endl;

    // Parse known chunks
    if (chunk_id == 0x4F464E49) // "INFO"
    {
      has_info = parseINFO(&data[offset], chunk_size);
    }
    else if (chunk_id == 0x50414D54) // "TMAP"
    {
      has_tmap = parseTMAP(&data[offset], chunk_size);
    }
    else if (chunk_id == 0x534B5254) // "TRKS"
    {
      has_trks = parseTRKS(&data[offset], chunk_size);
    }

    // Move to next chunk
    offset += chunk_size;
  }

  if (!has_info || !has_tmap || !has_trks)
  {
    std::cerr << "Missing required WOZ2 chunks (INFO: " << has_info
              << ", TMAP: " << has_tmap << ", TRKS: " << has_trks << ")" << std::endl;
    return false;
  }

  // Set write protection from INFO chunk
  write_protected_ = woz2_info_.write_protected;

  std::cout << "WOZ2 disk loaded successfully" << std::endl;
  std::cout << "  Disk type: " << (woz2_info_.disk_type == 1 ? "5.25\"" : "3.5\"") << std::endl;
  std::cout << "  Write protected: " << (write_protected_ ? "Yes" : "No") << std::endl;
  std::cout << "  Creator: " << woz2_info_.creator << std::endl;

  return true;
}

bool DiskImage::parseINFO(const uint8_t *chunk_data, uint32_t chunk_size)
{
  if (chunk_size < 60)
  {
    std::cerr << "INFO chunk too small" << std::endl;
    return false;
  }

  woz2_info_.version = chunk_data[0];
  woz2_info_.disk_type = chunk_data[1];
  woz2_info_.write_protected = chunk_data[2] != 0;
  woz2_info_.synchronized = chunk_data[3] != 0;
  woz2_info_.cleaned = chunk_data[4] != 0;

  // Creator (32 bytes starting at offset 5, UTF-8)
  woz2_info_.creator.assign(reinterpret_cast<const char *>(&chunk_data[5]), 32);
  // Trim null padding
  size_t null_pos = woz2_info_.creator.find('\0');
  if (null_pos != std::string::npos)
  {
    woz2_info_.creator.resize(null_pos);
  }

  woz2_info_.disk_sides = chunk_data[37];
  woz2_info_.boot_sector_format = chunk_data[38];
  woz2_info_.optimal_bit_timing = chunk_data[39];
  woz2_info_.compatible_hardware = chunk_data[40] | (chunk_data[41] << 8);
  woz2_info_.required_ram = chunk_data[42] | (chunk_data[43] << 8);
  woz2_info_.largest_track = chunk_data[44] | (chunk_data[45] << 8);

  std::cout << "INFO: version=" << static_cast<int>(woz2_info_.version)
            << ", disk_type=" << static_cast<int>(woz2_info_.disk_type)
            << ", largest_track=" << woz2_info_.largest_track << " blocks" << std::endl;

  return true;
}

bool DiskImage::parseTMAP(const uint8_t *chunk_data, uint32_t chunk_size)
{
  if (chunk_size < 160)
  {
    std::cerr << "TMAP chunk too small" << std::endl;
    return false;
  }

  // Copy track map (160 bytes)
  std::copy(chunk_data, chunk_data + 160, track_map_.begin());

  // Count non-empty tracks for debugging
  int track_count = 0;
  for (int i = 0; i < WOZ2_MAX_TRACKS; i++)
  {
    if (track_map_[i] != 0xFF)
    {
      track_count++;
    }
  }

  std::cout << "TMAP: " << track_count << " non-empty track entries" << std::endl;

  return true;
}

bool DiskImage::parseTRKS(const uint8_t *chunk_data, uint32_t chunk_size)
{
  // TRKS chunk structure:
  // - 160 TRK entries (8 bytes each) = 1280 bytes
  // - Track data starts at 512-byte aligned offset (block 3 = byte 1536 in file)

  if (chunk_size < 1280)
  {
    std::cerr << "TRKS chunk too small" << std::endl;
    return false;
  }

  // Parse TRK entries
  for (int i = 0; i < WOZ2_MAX_TRACKS; i++)
  {
    const uint8_t *trk_entry = &chunk_data[i * 8];

    woz2_tracks_[i].starting_block = trk_entry[0] | (trk_entry[1] << 8);
    woz2_tracks_[i].block_count = trk_entry[2] | (trk_entry[3] << 8);
    woz2_tracks_[i].bit_count = trk_entry[4] | (trk_entry[5] << 8) |
                                (trk_entry[6] << 16) | (trk_entry[7] << 24);
  }

  // Load track data
  // Track data starts at offset 1536 within the chunk (block 3)
  size_t track_data_offset = 1536;

  for (int i = 0; i < WOZ2_MAX_TRACKS; i++)
  {
    if (woz2_tracks_[i].block_count > 0)
    {
      size_t byte_count = woz2_tracks_[i].block_count * 512;
      size_t data_offset = track_data_offset + (woz2_tracks_[i].starting_block * 512);

      if (data_offset + byte_count > chunk_size)
      {
        std::cerr << "Track " << i << " data extends beyond TRKS chunk:" << std::endl;
        std::cerr << "  starting_block=" << woz2_tracks_[i].starting_block
                  << ", block_count=" << woz2_tracks_[i].block_count
                  << ", bit_count=" << woz2_tracks_[i].bit_count << std::endl;
        std::cerr << "  data_offset=" << data_offset
                  << ", byte_count=" << byte_count
                  << ", chunk_size=" << chunk_size << std::endl;
        continue;
      }

      woz2_tracks_[i].data.assign(&chunk_data[data_offset], &chunk_data[data_offset + byte_count]);
    }
  }

  // Count loaded tracks for debugging
  int loaded_tracks = 0;
  for (int i = 0; i < WOZ2_MAX_TRACKS; i++)
  {
    if (!woz2_tracks_[i].data.empty())
    {
      loaded_tracks++;
    }
  }

  std::cout << "TRKS: Loaded " << loaded_tracks << " tracks with bitstream data" << std::endl;

  return true;
}

uint8_t DiskImage::getBitFromWOZ2(int track, int bit_position) const
{
  // Map whole track to quarter-track index (track * 4)
  int quarter_track = track * 4;
  if (quarter_track >= WOZ2_MAX_TRACKS)
  {
    return 0;
  }

  uint8_t tmap_index = track_map_[quarter_track];
  if (tmap_index == 0xFF || tmap_index >= WOZ2_MAX_TRACKS)
  {
    return 0; // Empty track
  }

  const WOZ2Track &woz_track = woz2_tracks_[tmap_index];
  if (woz_track.data.empty() || woz_track.bit_count == 0)
  {
    return 0;
  }

  // Wrap bit position within track
  bit_position = bit_position % woz_track.bit_count;

  // Get byte and bit offset
  int byte_offset = bit_position / 8;
  int bit_offset = 7 - (bit_position % 8); // MSB first

  if (byte_offset >= static_cast<int>(woz_track.data.size()))
  {
    return 0;
  }

  uint8_t bit = (woz_track.data[byte_offset] >> bit_offset) & 1;

  // MC3470 emulation for weak bits
  // Shift buffer and check for 0x00 (all zeros = weak bit condition)
  mc3470_buffer_ = ((mc3470_buffer_ << 1) | bit) & 0x0F;
  mc3470_bit_count_++;

  if (mc3470_bit_count_ >= 4)
  {
    if (mc3470_buffer_ == 0x00)
    {
      // Weak bit detected - return random bit
      return getWeakBit();
    }
  }

  return bit;
}

uint8_t DiskImage::getWeakBit() const
{
  // 30% probability of returning 1, 70% probability of returning 0
  std::uniform_int_distribution<int> dist(1, 10);
  return (dist(rng_) <= 3) ? 1 : 0;
}
