#include "emulator/disk_formats/gcr_encoding.hpp"
#include <algorithm>
#include <cstring>

namespace GCR
{

std::vector<uint8_t> encode6and2(const uint8_t *data)
{
  // 6-and-2 encoding using the apple2js algorithm (from working git commit ad1ccb8)
  // This produces 342 nibbles + 1 checksum = 343 total
  //
  // The 342-byte nibble buffer is organized as:
  // - Bytes 0-85 (0x00-0x55): Auxiliary nibbles (low 2 bits)
  // - Bytes 86-341 (0x56-0x155): Data nibbles (high 6 bits)

  // Use larger buffer to accommodate the algorithm's writes beyond 0x155
  // The algorithm writes to indices up to 0x56 + 0x101 = 0x157
  uint8_t nibbles[0x158]; // 344 bytes (extra 2 for algorithm overflow)
  std::memset(nibbles, 0, sizeof(nibbles));

  const int ptr2 = 0;      // Auxiliary nibbles at offset 0
  const int ptr6 = 0x56;   // Data nibbles at offset 86

  // Process 258 iterations (0x101 down to 0) into auxiliary and data nibbles
  // This is the exact algorithm from the working version
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

  // Encode nibbles with XOR chaining and lookup table
  // Only output the first 342 nibbles (0x156)
  std::vector<uint8_t> result;
  result.reserve(343);

  uint8_t last = 0;
  for (int i = 0; i < 0x156; ++i)
  {
    uint8_t val = nibbles[i];
    result.push_back(ENCODE_6_AND_2[last ^ val]);
    last = val;
  }
  result.push_back(ENCODE_6_AND_2[last]); // Final checksum nibble

  return result;
}

std::vector<uint8_t> buildSector(uint8_t volume, uint8_t track,
                                 uint8_t sector, const uint8_t *data)
{
  std::vector<uint8_t> nibbles;
  nibbles.reserve(400); // Approximate sector size

  // Gap 1: Self-sync bytes before address field (~12-16 sync bytes)
  for (int i = 0; i < 14; i++)
  {
    nibbles.push_back(SYNC_BYTE);
  }

  // Address field prologue: D5 AA 96
  nibbles.push_back(ADDR_PROLOGUE[0]);
  nibbles.push_back(ADDR_PROLOGUE[1]);
  nibbles.push_back(ADDR_PROLOGUE[2]);

  // Address field data (4-and-4 encoded): volume, track, sector, checksum
  auto [vol_odd, vol_even] = encode4and4(volume);
  nibbles.push_back(vol_odd);
  nibbles.push_back(vol_even);

  auto [trk_odd, trk_even] = encode4and4(track);
  nibbles.push_back(trk_odd);
  nibbles.push_back(trk_even);

  auto [sec_odd, sec_even] = encode4and4(sector);
  nibbles.push_back(sec_odd);
  nibbles.push_back(sec_even);

  // Checksum is XOR of volume, track, sector
  uint8_t addr_checksum = volume ^ track ^ sector;
  auto [chk_odd, chk_even] = encode4and4(addr_checksum);
  nibbles.push_back(chk_odd);
  nibbles.push_back(chk_even);

  // Address field epilogue: DE AA EB
  nibbles.push_back(ADDR_EPILOGUE[0]);
  nibbles.push_back(ADDR_EPILOGUE[1]);
  nibbles.push_back(ADDR_EPILOGUE[2]);

  // Gap 2: Sync bytes between address and data (~5-7 sync bytes)
  for (int i = 0; i < 6; i++)
  {
    nibbles.push_back(SYNC_BYTE);
  }

  // Data field prologue: D5 AA AD
  nibbles.push_back(DATA_PROLOGUE[0]);
  nibbles.push_back(DATA_PROLOGUE[1]);
  nibbles.push_back(DATA_PROLOGUE[2]);

  // Data field: 343 nibbles (6-and-2 encoded 256 bytes + checksum)
  std::vector<uint8_t> encoded_data = encode6and2(data);
  nibbles.insert(nibbles.end(), encoded_data.begin(), encoded_data.end());

  // Data field epilogue: DE AA EB
  nibbles.push_back(DATA_EPILOGUE[0]);
  nibbles.push_back(DATA_EPILOGUE[1]);
  nibbles.push_back(DATA_EPILOGUE[2]);

  return nibbles;
}

} // namespace GCR
