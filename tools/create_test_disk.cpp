#include <iostream>
#include <fstream>
#include <cstdint>
#include <vector>
#include <iomanip>

// Apple II DSK format constants
constexpr int TRACKS = 35;
constexpr int SECTORS_PER_TRACK = 16;
constexpr int BYTES_PER_SECTOR = 256;
constexpr int DISK_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;  // 143,360 bytes

/**
 * Create a synthetic test disk with known, predictable data patterns.
 *
 * Each sector contains:
 *   Byte 0:     Track number
 *   Byte 1:     Sector number
 *   Bytes 2-3:  0xAA, 0x55 (test pattern)
 *   Bytes 4-7:  ASCII "TEST"
 *   Bytes 8-255: Incrementing counter (byte offset % 256)
 *
 * This allows easy verification and debugging of disk operations.
 */
int main(int argc, char* argv[])
{
  std::string outputPath = "test_disk.dsk";

  if (argc > 1)
  {
    outputPath = argv[1];
  }

  std::cout << "Creating synthetic test disk: " << outputPath << std::endl;
  std::cout << "  Tracks: " << TRACKS << std::endl;
  std::cout << "  Sectors per track: " << SECTORS_PER_TRACK << std::endl;
  std::cout << "  Bytes per sector: " << BYTES_PER_SECTOR << std::endl;
  std::cout << "  Total size: " << DISK_SIZE << " bytes" << std::endl;
  std::cout << std::endl;

  // Create disk data
  std::vector<uint8_t> diskData(DISK_SIZE);

  // Fill each sector with predictable pattern
  for (int track = 0; track < TRACKS; track++)
  {
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
    {
      // Calculate sector offset in DSK file
      size_t offset = (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR;
      uint8_t* sectorData = &diskData[offset];

      // Byte 0: Track number
      sectorData[0] = static_cast<uint8_t>(track);

      // Byte 1: Sector number
      sectorData[1] = static_cast<uint8_t>(sector);

      // Bytes 2-3: Test pattern (alternating bits)
      sectorData[2] = 0xAA;
      sectorData[3] = 0x55;

      // Bytes 4-7: ASCII "TEST"
      sectorData[4] = 'T';
      sectorData[5] = 'E';
      sectorData[6] = 'S';
      sectorData[7] = 'T';

      // Bytes 8-255: Incrementing counter
      for (int i = 8; i < BYTES_PER_SECTOR; i++)
      {
        sectorData[i] = static_cast<uint8_t>(i);
      }
    }
  }

  // Write to file
  std::ofstream file(outputPath, std::ios::binary);
  if (!file)
  {
    std::cerr << "ERROR: Cannot create output file: " << outputPath << std::endl;
    return 1;
  }

  file.write(reinterpret_cast<const char*>(diskData.data()), DISK_SIZE);
  if (!file)
  {
    std::cerr << "ERROR: Failed to write disk data" << std::endl;
    return 1;
  }

  file.close();

  std::cout << "Success! Created " << outputPath << std::endl;
  std::cout << std::endl;

  // Show sample data from a few sectors
  std::cout << "Sample sector data:" << std::endl;
  std::cout << std::endl;

  // Show Track 0, Sector 0
  std::cout << "Track 0, Sector 0 (first 32 bytes):" << std::endl;
  std::cout << "  ";
  for (int i = 0; i < 32; i++)
  {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(diskData[i]) << " ";
  }
  std::cout << std::dec << std::endl;
  std::cout << "  Expected: 00 00 AA 55 54 45 53 54 08 09 0A 0B 0C 0D 0E 0F..." << std::endl;
  std::cout << std::endl;

  // Show Track 1, Sector 5
  size_t offset = (1 * SECTORS_PER_TRACK + 5) * BYTES_PER_SECTOR;
  std::cout << "Track 1, Sector 5 (first 32 bytes):" << std::endl;
  std::cout << "  ";
  for (int i = 0; i < 32; i++)
  {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(diskData[offset + i]) << " ";
  }
  std::cout << std::dec << std::endl;
  std::cout << "  Expected: 01 05 AA 55 54 45 53 54 08 09 0A 0B 0C 0D 0E 0F..." << std::endl;
  std::cout << std::endl;

  // Show Track 34, Sector 15 (last sector)
  offset = (34 * SECTORS_PER_TRACK + 15) * BYTES_PER_SECTOR;
  std::cout << "Track 34, Sector 15 (first 32 bytes):" << std::endl;
  std::cout << "  ";
  for (int i = 0; i < 32; i++)
  {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(diskData[offset + i]) << " ";
  }
  std::cout << std::dec << std::endl;
  std::cout << "  Expected: 22 0F AA 55 54 45 53 54 08 09 0A 0B 0C 0D 0E 0F..." << std::endl;
  std::cout << std::endl;

  std::cout << "Pattern explanation:" << std::endl;
  std::cout << "  Byte 0:     Track number (0x00-0x22)" << std::endl;
  std::cout << "  Byte 1:     Sector number (0x00-0x0F)" << std::endl;
  std::cout << "  Bytes 2-3:  0xAA, 0x55 (test pattern)" << std::endl;
  std::cout << "  Bytes 4-7:  ASCII 'TEST' (0x54 0x45 0x53 0x54)" << std::endl;
  std::cout << "  Bytes 8-FF: Incrementing counter (0x08-0xFF)" << std::endl;
  std::cout << std::endl;

  std::cout << "You can now use this disk to test disk operations!" << std::endl;
  std::cout << "The predictable pattern makes it easy to verify reads/writes." << std::endl;

  return 0;
}
