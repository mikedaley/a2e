#include <iostream>
#include <fstream>
#include <cstdint>
#include <vector>
#include <iomanip>

// Apple II DSK format constants
constexpr int TRACKS = 35;
constexpr int SECTORS_PER_TRACK = 16;
constexpr int BYTES_PER_SECTOR = 256;
constexpr int DISK_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;

/**
 * Verify a synthetic test disk created by create_test_disk
 */
int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <disk_image.dsk>" << std::endl;
    return 1;
  }

  std::string filepath = argv[1];
  std::cout << "Verifying test disk: " << filepath << std::endl;

  // Load disk
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file)
  {
    std::cerr << "ERROR: Cannot open file: " << filepath << std::endl;
    return 1;
  }

  std::streamsize size = file.tellg();
  if (size != DISK_SIZE)
  {
    std::cerr << "ERROR: Invalid file size: " << size << " (expected " << DISK_SIZE << ")" << std::endl;
    return 1;
  }

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> diskData(DISK_SIZE);
  if (!file.read(reinterpret_cast<char*>(diskData.data()), DISK_SIZE))
  {
    std::cerr << "ERROR: Failed to read disk data" << std::endl;
    return 1;
  }

  std::cout << "File size OK: " << size << " bytes" << std::endl;
  std::cout << std::endl;

  // Verify each sector
  int errors = 0;
  int totalSectors = TRACKS * SECTORS_PER_TRACK;

  for (int track = 0; track < TRACKS; track++)
  {
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
    {
      size_t offset = (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR;
      const uint8_t* sectorData = &diskData[offset];

      // Verify expected pattern
      bool sectorOK = true;

      // Byte 0: Track number
      if (sectorData[0] != static_cast<uint8_t>(track))
      {
        std::cout << "ERROR T" << track << " S" << sector
                  << ": Byte 0 expected 0x" << std::hex << std::setw(2) << std::setfill('0') << track
                  << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(sectorData[0])
                  << std::dec << std::endl;
        sectorOK = false;
      }

      // Byte 1: Sector number
      if (sectorData[1] != static_cast<uint8_t>(sector))
      {
        std::cout << "ERROR T" << track << " S" << sector
                  << ": Byte 1 expected 0x" << std::hex << std::setw(2) << std::setfill('0') << sector
                  << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(sectorData[1])
                  << std::dec << std::endl;
        sectorOK = false;
      }

      // Bytes 2-3: Pattern
      if (sectorData[2] != 0xAA || sectorData[3] != 0x55)
      {
        std::cout << "ERROR T" << track << " S" << sector
                  << ": Bytes 2-3 expected AA 55, got "
                  << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sectorData[2])
                  << " " << std::setw(2) << std::setfill('0') << static_cast<int>(sectorData[3])
                  << std::dec << std::endl;
        sectorOK = false;
      }

      // Bytes 4-7: "TEST"
      if (sectorData[4] != 'T' || sectorData[5] != 'E' ||
          sectorData[6] != 'S' || sectorData[7] != 'T')
      {
        std::cout << "ERROR T" << track << " S" << sector
                  << ": Bytes 4-7 expected 'TEST', got '"
                  << static_cast<char>(sectorData[4])
                  << static_cast<char>(sectorData[5])
                  << static_cast<char>(sectorData[6])
                  << static_cast<char>(sectorData[7])
                  << "'" << std::endl;
        sectorOK = false;
      }

      // Bytes 8-255: Incrementing counter
      for (int i = 8; i < BYTES_PER_SECTOR; i++)
      {
        if (sectorData[i] != static_cast<uint8_t>(i))
        {
          std::cout << "ERROR T" << track << " S" << sector
                    << ": Byte " << i << " expected 0x"
                    << std::hex << std::setw(2) << std::setfill('0') << i
                    << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(sectorData[i])
                    << std::dec << std::endl;
          sectorOK = false;
          break;  // Only report first counter error per sector
        }
      }

      if (!sectorOK)
      {
        errors++;
        // Show first 32 bytes of problematic sector
        std::cout << "  First 32 bytes: ";
        for (int i = 0; i < 32; i++)
        {
          std::cout << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(sectorData[i]) << " ";
        }
        std::cout << std::dec << std::endl << std::endl;
      }
    }
  }

  std::cout << std::endl;
  std::cout << "Verification complete!" << std::endl;
  std::cout << "  Total sectors: " << totalSectors << std::endl;
  std::cout << "  Sectors with errors: " << errors << std::endl;
  std::cout << "  Sectors OK: " << (totalSectors - errors) << std::endl;

  if (errors == 0)
  {
    std::cout << std::endl;
    std::cout << "SUCCESS: All sectors verified correctly!" << std::endl;
    return 0;
  }
  else
  {
    std::cout << std::endl;
    std::cout << "FAILURE: " << errors << " sectors have errors" << std::endl;
    return 1;
  }
}
