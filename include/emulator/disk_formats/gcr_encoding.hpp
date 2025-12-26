#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

/**
 * GCR Encoding utilities for Apple II disk formats
 *
 * The Apple II Disk II controller uses GCR (Group Coded Recording) to encode
 * data on floppy disks. Two encoding schemes are used:
 *
 * 1. 4-and-4 encoding: Used for address field data (volume, track, sector)
 *    - Splits a byte into odd and even bits
 *    - Each half is OR'd with 0xAA to ensure valid disk bytes
 *
 * 2. 6-and-2 encoding: Used for sector data
 *    - Converts 256 bytes into 342 nibbles (343 with checksum)
 *    - Each nibble has bit 7 set and at most one consecutive zero
 */
namespace GCR
{

// 6-and-2 encoding lookup table
// Maps 6-bit values (0-63) to valid disk nibbles (bit 7 set, no adjacent zeros)
constexpr std::array<uint8_t, 64> ENCODE_6_AND_2 = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// Address field markers
constexpr uint8_t ADDR_PROLOGUE[3] = {0xD5, 0xAA, 0x96};
constexpr uint8_t ADDR_EPILOGUE[3] = {0xDE, 0xAA, 0xEB};

// Data field markers
constexpr uint8_t DATA_PROLOGUE[3] = {0xD5, 0xAA, 0xAD};
constexpr uint8_t DATA_EPILOGUE[3] = {0xDE, 0xAA, 0xEB};

// Self-sync byte
constexpr uint8_t SYNC_BYTE = 0xFF;

/**
 * 4-and-4 encoding for address field values
 * Splits byte into odd bits (first) and even bits (second)
 * Each byte is OR'd with 0xAA to ensure high bit set
 *
 * @param value Byte to encode
 * @return Pair of encoded bytes (odd bits, even bits)
 */
inline std::pair<uint8_t, uint8_t> encode4and4(uint8_t value)
{
  // Odd bits: bits 7,5,3,1 go to positions 6,4,2,0
  uint8_t odd = 0xAA | ((value >> 1) & 0x55);
  // Even bits: bits 6,4,2,0 stay in positions 6,4,2,0
  uint8_t even = 0xAA | (value & 0x55);
  return {odd, even};
}

/**
 * Encode 256 bytes of sector data using 6-and-2 encoding
 * Returns 343 nibbles (342 encoded data + 1 checksum)
 *
 * @param data Pointer to 256 bytes of sector data
 * @return Vector of 343 encoded nibbles
 */
std::vector<uint8_t> encode6and2(const uint8_t *data);

/**
 * Build a complete sector as a nibble stream
 * Includes sync bytes, address field, gap, and data field
 *
 * @param volume Volume number (typically 254)
 * @param track Track number (0-34)
 * @param sector Sector number (0-15)
 * @param data Pointer to 256 bytes of sector data
 * @return Vector of nibbles for the complete sector
 */
std::vector<uint8_t> buildSector(uint8_t volume, uint8_t track,
                                 uint8_t sector, const uint8_t *data);

} // namespace GCR
