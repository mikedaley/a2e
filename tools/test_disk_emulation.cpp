#include "emulator/disk_ii.hpp"
#include "emulator/disk_image.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

/**
 * Test application to verify the Disk II and DiskImage implementation
 * using the synthetic test disk.
 */

// Expected pattern in test disk sectors
constexpr int TRACKS = 35;
constexpr int SECTORS_PER_TRACK = 16;
constexpr int BYTES_PER_SECTOR = 256;

// Verify a sector's data matches the expected test pattern
bool verifySector(int track, int sector, const uint8_t* data)
{
  bool ok = true;

  // Byte 0: Track number
  if (data[0] != static_cast<uint8_t>(track))
  {
    std::cout << "  ERROR: Byte 0 expected 0x" << std::hex << std::setw(2) << std::setfill('0') << track
              << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(data[0])
              << std::dec << std::endl;
    ok = false;
  }

  // Byte 1: Sector number
  if (data[1] != static_cast<uint8_t>(sector))
  {
    std::cout << "  ERROR: Byte 1 expected 0x" << std::hex << std::setw(2) << std::setfill('0') << sector
              << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(data[1])
              << std::dec << std::endl;
    ok = false;
  }

  // Bytes 2-3: Pattern
  if (data[2] != 0xAA || data[3] != 0x55)
  {
    std::cout << "  ERROR: Bytes 2-3 expected AA 55, got "
              << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[2])
              << " " << std::setw(2) << std::setfill('0') << static_cast<int>(data[3])
              << std::dec << std::endl;
    ok = false;
  }

  // Bytes 4-7: "TEST"
  if (data[4] != 'T' || data[5] != 'E' || data[6] != 'S' || data[7] != 'T')
  {
    std::cout << "  ERROR: Bytes 4-7 expected 'TEST', got '"
              << static_cast<char>(data[4])
              << static_cast<char>(data[5])
              << static_cast<char>(data[6])
              << static_cast<char>(data[7])
              << "'" << std::endl;
    ok = false;
  }

  // Bytes 8-255: Incrementing counter
  for (int i = 8; i < BYTES_PER_SECTOR; i++)
  {
    if (data[i] != static_cast<uint8_t>(i))
    {
      std::cout << "  ERROR: Byte " << i << " expected 0x"
                << std::hex << std::setw(2) << std::setfill('0') << i
                << " got 0x" << std::setw(2) << std::setfill('0') << static_cast<int>(data[i])
                << std::dec << std::endl;
      ok = false;
      break;  // Only report first counter error
    }
  }

  return ok;
}

// Print first 32 bytes of sector data
void printSectorData(const uint8_t* data)
{
  std::cout << "  First 32 bytes: ";
  for (int i = 0; i < 32; i++)
  {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(data[i]) << " ";
  }
  std::cout << std::dec << std::endl;
}

int main(int argc, char* argv[])
{
  std::string diskPath = "resources/roms/disk/test_disk.dsk";
  if (argc > 1)
  {
    diskPath = argv[1];
  }

  std::cout << "========================================" << std::endl;
  std::cout << "Disk II Emulation Test" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << std::endl;
  std::cout << "Test disk: " << diskPath << std::endl;
  std::cout << std::endl;

  // ========================================
  // Test 1: Direct DiskImage loading
  // ========================================
  std::cout << "TEST 1: Direct DiskImage Loading" << std::endl;
  std::cout << "------------------------------------" << std::endl;

  DiskImage diskImage;
  if (!diskImage.load(diskPath))
  {
    std::cerr << "ERROR: Failed to load disk image" << std::endl;
    return 1;
  }

  std::cout << "Disk loaded successfully" << std::endl;
  std::cout << "  Format: " << (diskImage.getFormat() == DiskFormat::DOS33 ? "DOS 3.3" : "ProDOS") << std::endl;
  std::cout << std::endl;

  // Verify a few sectors using direct access
  std::cout << "Verifying sectors using direct access..." << std::endl;

  int directErrors = 0;
  std::vector<std::pair<int, int>> testSectors = {
    {0, 0}, {0, 15}, {1, 5}, {17, 8}, {34, 15}
  };

  for (const auto& [track, sector] : testSectors)
  {
    const uint8_t* data = diskImage.getSector(track, sector);
    if (!data)
    {
      std::cout << "ERROR: Could not read T" << track << " S" << sector << std::endl;
      directErrors++;
      continue;
    }

    std::cout << "T" << std::setw(2) << track << " S" << std::setw(2) << sector << ": ";
    if (verifySector(track, sector, data))
    {
      std::cout << "OK" << std::endl;
    }
    else
    {
      printSectorData(data);
      directErrors++;
    }
  }

  std::cout << std::endl;
  std::cout << "Direct access test: " << (directErrors == 0 ? "PASSED" : "FAILED")
            << " (" << directErrors << " errors)" << std::endl;
  std::cout << std::endl;

  // ========================================
  // Test 2: Complete Disk Verification
  // ========================================
  std::cout << "TEST 2: Complete Disk Verification" << std::endl;
  std::cout << "------------------------------------" << std::endl;
  std::cout << "Verifying ALL sectors (35 tracks × 16 sectors = 560 total)" << std::endl;
  std::cout << std::endl;

  int allSectorsErrors = 0;
  int totalSectors = 0;
  int verifiedSectors = 0;

  for (int track = 0; track < TRACKS; track++)
  {
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
    {
      totalSectors++;
      const uint8_t* data = diskImage.getSector(track, sector);

      if (!data)
      {
        std::cout << "ERROR: Could not read T" << track << " S" << sector << std::endl;
        allSectorsErrors++;
        continue;
      }

      if (verifySector(track, sector, data))
      {
        verifiedSectors++;
      }
      else
      {
        std::cout << "T" << std::setw(2) << track << " S" << std::setw(2) << sector << ": FAILED" << std::endl;
        printSectorData(data);
        allSectorsErrors++;
      }
    }
  }

  std::cout << "Verified " << verifiedSectors << " / " << totalSectors << " sectors" << std::endl;
  std::cout << std::endl;
  std::cout << "Complete disk verification: " << (allSectorsErrors == 0 ? "PASSED" : "FAILED")
            << " (" << allSectorsErrors << " errors)" << std::endl;
  std::cout << std::endl;

  // ========================================
  // Test 3: Nibblization and Denibblization
  // ========================================
  std::cout << "TEST 3: Nibblization Round-Trip Test" << std::endl;
  std::cout << "------------------------------------" << std::endl;
  std::cout << "Testing ALL 35 tracks through nibblization/denibblization" << std::endl;
  std::cout << std::endl;

  int nibbleErrors = 0;
  int tracksVerified = 0;

  // Test ALL tracks through nibblization/denibblization
  for (int track = 0; track < TRACKS; track++)
  {
    // Progress indicator every 5 tracks
    if (track % 5 == 0)
    {
      std::cout << "Testing tracks " << track << "-" << std::min(track + 4, TRACKS - 1) << "..." << std::endl;
    }

    // Save original data
    std::vector<uint8_t> originalData(SECTORS_PER_TRACK * BYTES_PER_SECTOR);
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
    {
      const uint8_t* sectorData = diskImage.getSector(track, sector);
      if (!sectorData)
      {
        std::cout << "  ERROR T" << track << ": Could not read sector " << sector << std::endl;
        nibbleErrors++;
        continue;
      }
      std::memcpy(&originalData[sector * BYTES_PER_SECTOR], sectorData, BYTES_PER_SECTOR);
    }

    // Nibblize the track
    auto nibbleData = diskImage.getNibblizedTrack(track);

    if (nibbleData.empty())
    {
      std::cout << "  ERROR T" << track << ": Nibblization failed" << std::endl;
      nibbleErrors++;
      continue;
    }

    // Create a new disk image and decode the track into it
    DiskImage testImage;
    if (!testImage.decodeTrack(track, nibbleData))
    {
      std::cout << "  ERROR T" << track << ": Denibblization failed" << std::endl;
      nibbleErrors++;
      continue;
    }

    // Verify all sectors in the track
    bool trackOK = true;
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++)
    {
      const uint8_t* decoded = testImage.getSector(track, sector);
      if (!decoded)
      {
        std::cout << "  ERROR T" << track << " S" << sector << ": Could not read decoded sector" << std::endl;
        trackOK = false;
        nibbleErrors++;
        continue;
      }

      // Compare with original
      const uint8_t* original = &originalData[sector * BYTES_PER_SECTOR];
      if (std::memcmp(original, decoded, BYTES_PER_SECTOR) != 0)
      {
        std::cout << "  ERROR T" << track << " S" << sector << ": Mismatch after encode/decode" << std::endl;
        std::cout << "    Original: ";
        for (int i = 0; i < 16; i++)
        {
          std::cout << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(original[i]) << " ";
        }
        std::cout << std::dec << std::endl;
        std::cout << "    Decoded:  ";
        for (int i = 0; i < 16; i++)
        {
          std::cout << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(decoded[i]) << " ";
        }
        std::cout << std::dec << std::endl;
        trackOK = false;
        nibbleErrors++;
      }

      // Also verify the pattern
      if (!verifySector(track, sector, decoded))
      {
        std::cout << "  ERROR T" << track << " S" << sector << ": Pattern verification failed" << std::endl;
        trackOK = false;
        nibbleErrors++;
      }
    }

    if (trackOK)
    {
      tracksVerified++;
    }
  }

  std::cout << std::endl;
  std::cout << "Nibblization test: " << tracksVerified << " / " << TRACKS << " tracks verified" << std::endl;

  std::cout << std::endl;
  std::cout << "Nibblization test: " << (nibbleErrors == 0 ? "PASSED" : "FAILED")
            << " (" << nibbleErrors << " errors)" << std::endl;
  std::cout << std::endl;

  // ========================================
  // Test 4: Disk II Controller Integration
  // ========================================
  std::cout << "TEST 4: Disk II Controller Integration" << std::endl;
  std::cout << "------------------------------------" << std::endl;

  // Create Disk II controller in slot 6
  DiskII diskII(6);

  // Insert test disk into drive 1
  if (!diskII.insertDisk(0, diskPath))
  {
    std::cerr << "ERROR: Failed to insert disk into Disk II" << std::endl;
    return 1;
  }

  std::cout << "Disk inserted into Drive 1" << std::endl;
  std::cout << "  Disk inserted: " << (diskII.isDiskInserted(0) ? "Yes" : "No") << std::endl;
  std::cout << "  Write protected: " << (diskII.isWriteProtected(0) ? "Yes" : "No") << std::endl;
  std::cout << std::endl;

  // Get the disk image from the controller
  const DiskImage* controllerDisk = diskII.getDiskImage(0);
  if (!controllerDisk)
  {
    std::cerr << "ERROR: Could not get disk image from controller" << std::endl;
    return 1;
  }

  std::cout << "Testing sector access through Disk II controller..." << std::endl;

  int controllerErrors = 0;
  for (const auto& [track, sector] : testSectors)
  {
    const uint8_t* data = controllerDisk->getSector(track, sector);
    if (!data)
    {
      std::cout << "ERROR: Could not read T" << track << " S" << sector << std::endl;
      controllerErrors++;
      continue;
    }

    std::cout << "T" << std::setw(2) << track << " S" << std::setw(2) << sector << ": ";
    if (verifySector(track, sector, data))
    {
      std::cout << "OK" << std::endl;
    }
    else
    {
      printSectorData(data);
      controllerErrors++;
    }
  }

  std::cout << std::endl;
  std::cout << "Disk II integration test: " << (controllerErrors == 0 ? "PASSED" : "FAILED")
            << " (" << controllerErrors << " errors)" << std::endl;
  std::cout << std::endl;

  // ========================================
  // Summary
  // ========================================
  std::cout << "========================================" << std::endl;
  std::cout << "Test Summary" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << std::endl;

  int totalErrors = directErrors + allSectorsErrors + nibbleErrors + controllerErrors;

  std::cout << "Test 1 - Direct Access:       " << (directErrors == 0 ? "PASSED" : "FAILED")
            << " (" << directErrors << " errors)" << std::endl;
  std::cout << "Test 2 - All Sectors:         " << (allSectorsErrors == 0 ? "PASSED" : "FAILED")
            << " (" << verifiedSectors << "/" << totalSectors << " sectors verified, "
            << allSectorsErrors << " errors)" << std::endl;
  std::cout << "Test 3 - Nibblization:        " << (nibbleErrors == 0 ? "PASSED" : "FAILED")
            << " (" << tracksVerified << "/" << TRACKS << " tracks verified, "
            << nibbleErrors << " errors)" << std::endl;
  std::cout << "Test 4 - Disk II Controller:  " << (controllerErrors == 0 ? "PASSED" : "FAILED")
            << " (" << controllerErrors << " errors)" << std::endl;
  std::cout << std::endl;

  if (totalErrors == 0)
  {
    std::cout << "ALL TESTS PASSED!" << std::endl;
    std::cout << std::endl;
    std::cout << "The disk emulation is working correctly:" << std::endl;
    std::cout << "  ✓ Disk images load properly" << std::endl;
    std::cout << "  ✓ ALL 560 sectors verified (35 tracks × 16 sectors)" << std::endl;
    std::cout << "  ✓ ALL 35 tracks survive nibblization round-trip" << std::endl;
    std::cout << "  ✓ 6-and-2 encoding/decoding works perfectly" << std::endl;
    std::cout << "  ✓ Disk II controller integration works" << std::endl;
    return 0;
  }
  else
  {
    std::cout << "TESTS FAILED!" << std::endl;
    std::cout << "Total errors: " << totalErrors << std::endl;
    return 1;
  }
}
