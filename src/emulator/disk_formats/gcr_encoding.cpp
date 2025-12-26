#include "emulator/disk_formats/gcr_encoding.hpp"
#include <algorithm>
#include <cstring>

namespace GCR
{

std::vector<uint8_t> encode6and2(const uint8_t *data)
{
  // The 6-and-2 encoding converts 256 bytes into 342 6-bit values
  // which are then encoded into 342 disk nibbles plus a checksum

  // Buffer for prenibblized data (342 bytes)
  // First 86 bytes: auxiliary buffer (2 bits from each of 256 bytes)
  // Next 256 bytes: primary buffer (6 bits from each data byte)
  uint8_t buffer[342];
  std::memset(buffer, 0, sizeof(buffer));

  // Step 1: Build auxiliary buffer (first 86 bytes)
  // Extract bits 0-1 from each data byte and pack them
  // Each aux byte holds bits from 3 data bytes (or 2 for the last few)
  for (int i = 0; i < 86; i++)
  {
    uint8_t val = 0;

    // Bits from data[i] (bits 0,1 -> bits 0,1 of aux)
    if (i < 86)
    {
      val = ((data[i] & 0x01) << 1) | ((data[i] & 0x02) >> 1);
    }

    // Bits from data[i+86] (bits 0,1 -> bits 2,3 of aux)
    if (i + 86 < 256)
    {
      val |= ((data[i + 86] & 0x01) << 3) | ((data[i + 86] & 0x02) << 1);
    }

    // Bits from data[i+172] (bits 0,1 -> bits 4,5 of aux)
    if (i + 172 < 256)
    {
      val |= ((data[i + 172] & 0x01) << 5) | ((data[i + 172] & 0x02) << 3);
    }

    buffer[i] = val;
  }

  // Step 2: Build primary buffer (next 256 bytes)
  // Store bits 2-7 from each data byte (shifted right by 2)
  for (int i = 0; i < 256; i++)
  {
    buffer[86 + i] = data[i] >> 2;
  }

  // Step 3: XOR encode and convert to disk nibbles
  // Each byte is XORed with the previous byte's value (differential encoding)
  // This allows the decoder to recover the original by XORing each value with previous
  std::vector<uint8_t> result;
  result.reserve(343);

  // XOR encode in forward order
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++)
  {
    uint8_t xor_val = buffer[i] ^ prev;
    result.push_back(ENCODE_6_AND_2[xor_val & 0x3F]);
    prev = buffer[i];
  }

  // Append checksum nibble (the last raw value before XOR)
  result.push_back(ENCODE_6_AND_2[prev & 0x3F]);

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
