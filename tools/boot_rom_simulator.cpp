#include "emulator/disk_ii.hpp"
#include "emulator/disk_image.hpp"
#include <iostream>
#include <iomanip>
#include <vector>

/**
 * Simulates the Apple II boot ROM sequence for Disk II
 * This mimics what happens at $C600 when booting from slot 6
 */

// Boot ROM constants
const uint8_t ADDRESS_PROLOGUE[] = {0xD5, 0xAA, 0x96};  // Address field marker
const uint8_t DATA_PROLOGUE[] = {0xD5, 0xAA, 0xAD};     // Data field marker

// Simulate reading the data latch like the ROM does
uint8_t readByte(DiskII& disk, uint64_t& cycle)
{
  cycle += 32;  // ~32 cycles per byte read
  disk.update(cycle);
  return disk.read(0xC0EC);  // Q6L - read data latch
}

// Decode 4-and-4 encoded value (2 bytes -> 1 byte)
uint8_t decode4and4(DiskII& disk, uint64_t& cycle)
{
  uint8_t byte1 = readByte(disk, cycle);  // Odd bits
  uint8_t byte2 = readByte(disk, cycle);  // Even bits
  // Reconstruct: (odd bits << 1 | 1) & even bits
  return ((byte1 << 1) | 0x01) & byte2;
}

// Search for a specific byte sequence (like finding prologue)
bool findSequence(DiskII& disk, uint64_t& cycle, const uint8_t* sequence, int length, int maxAttempts = 10000)
{
  std::vector<uint8_t> buffer(length, 0);
  int matchIndex = 0;

  for (int attempt = 0; attempt < maxAttempts; attempt++)
  {
    uint8_t byte = readByte(disk, cycle);

    if (byte == sequence[matchIndex])
    {
      matchIndex++;
      if (matchIndex == length)
      {
        std::cout << "  Found sequence after " << (attempt + 1) << " reads" << std::endl;
        return true;
      }
    }
    else
    {
      matchIndex = (byte == sequence[0]) ? 1 : 0;
    }
  }

  std::cout << "  ERROR: Sequence not found after " << maxAttempts << " attempts" << std::endl;
  return false;
}

int main(int argc, char* argv[])
{
  std::string diskPath = "../Apple DOS 3.3 January 1983.dsk";
  if (argc > 1)
  {
    diskPath = argv[1];
  }

  std::cout << "╔════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║    Apple II Boot ROM Simulator - Disk II      ║" << std::endl;
  std::cout << "╚════════════════════════════════════════════════╝" << std::endl;
  std::cout << std::endl;
  std::cout << "Disk image: " << diskPath << std::endl;
  std::cout << std::endl;

  // Create Disk II controller in slot 6
  DiskII diskII(6);

  // Insert disk
  std::cout << "Step 1: Inserting disk..." << std::endl;
  if (!diskII.insertDisk(0, diskPath))
  {
    std::cerr << "ERROR: Failed to insert disk" << std::endl;
    return 1;
  }
  std::cout << "  Disk inserted successfully" << std::endl;
  std::cout << std::endl;

  uint64_t cycle = 0;

  // Step 2: Turn on motor (what boot ROM does first)
  std::cout << "Step 2: Turning on drive motor..." << std::endl;
  diskII.update(cycle);
  diskII.write(0xC0E9, 0);  // MOTOR_ON
  cycle += 1000;  // Wait for motor to spin up
  std::cout << "  Motor on, waiting for spinup" << std::endl;
  std::cout << std::endl;

  // Step 3: Set read mode
  std::cout << "Step 3: Setting read mode (Q7L)..." << std::endl;
  diskII.update(cycle);
  diskII.read(0xC0EE);  // Q7L - read mode
  cycle += 10;
  std::cout << "  Read mode enabled" << std::endl;
  std::cout << std::endl;

  // Step 4: Search for address prologue on Track 0
  // The boot ROM looks for any sector on Track 0
  std::cout << "Step 4: Searching for address prologue (D5 AA 96)..." << std::endl;
  std::cout << "  (This is what the ROM does to find a sector)" << std::endl;

  if (!findSequence(diskII, cycle, ADDRESS_PROLOGUE, 3))
  {
    std::cerr << "ERROR: Could not find address prologue!" << std::endl;
    std::cerr << "This means the disk is not providing nibblized data correctly." << std::endl;
    return 1;
  }
  std::cout << "  SUCCESS: Found address prologue!" << std::endl;
  std::cout << std::endl;

  // Step 5: Read address field (Volume, Track, Sector, Checksum)
  // Each field is 4-and-4 encoded (2 bytes per field)
  std::cout << "Step 5: Reading address field (4-and-4 encoded)..." << std::endl;
  uint8_t volume = decode4and4(diskII, cycle);
  uint8_t track = decode4and4(diskII, cycle);
  uint8_t sector = decode4and4(diskII, cycle);
  uint8_t checksum = decode4and4(diskII, cycle);

  std::cout << "  Volume:   " << static_cast<int>(volume) << " (0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(volume) << ")" << std::endl;
  std::cout << "  Track:    " << std::dec << static_cast<int>(track) << " (0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(track) << ")" << std::endl;
  std::cout << "  Sector:   " << std::dec << static_cast<int>(sector) << " (0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sector) << ")" << std::endl;
  std::cout << "  Checksum: " << std::dec << static_cast<int>(checksum) << " (0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(checksum) << ")" << std::dec << std::endl;
  std::cout << std::endl;

  // Step 6: Search for data prologue
  std::cout << "Step 6: Searching for data prologue (D5 AA AD)..." << std::endl;

  if (!findSequence(diskII, cycle, DATA_PROLOGUE, 3, 100))
  {
    std::cerr << "ERROR: Could not find data prologue after address field!" << std::endl;
    return 1;
  }
  std::cout << "  SUCCESS: Found data prologue!" << std::endl;
  std::cout << std::endl;

  // Step 7: Read first 16 bytes of sector data (encoded)
  std::cout << "Step 7: Reading sector data (first 16 encoded bytes)..." << std::endl;
  std::cout << "  Data: ";
  for (int i = 0; i < 16; i++)
  {
    uint8_t byte = readByte(diskII, cycle);
    std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
  }
  std::cout << std::dec << std::endl;
  std::cout << std::endl;

  // Step 8: Boot ROM would now try to read Track 0, Sector 0 specifically
  // Let's try to find it
  std::cout << "Step 8: Searching for Track 0, Sector 0 specifically..." << std::endl;
  std::cout << "  (Boot ROM needs this sector to load the boot code)" << std::endl;

  int attempts = 0;
  const int MAX_SECTOR_SEARCH = 50;
  bool foundT0S0 = false;

  while (attempts < MAX_SECTOR_SEARCH && !foundT0S0)
  {
    // Find next address prologue
    if (!findSequence(diskII, cycle, ADDRESS_PROLOGUE, 3, 2000))
    {
      std::cerr << "ERROR: Lost sync with disk!" << std::endl;
      break;
    }

    // Read address field (4-and-4 decoded)
    uint8_t vol = decode4and4(diskII, cycle);
    uint8_t trk = decode4and4(diskII, cycle);
    uint8_t sec = decode4and4(diskII, cycle);
    uint8_t chk = decode4and4(diskII, cycle);

    std::cout << "  Attempt " << (attempts + 1) << ": Found T" << static_cast<int>(trk)
              << " S" << static_cast<int>(sec) << std::endl;

    if (trk == 0 && sec == 0)
    {
      foundT0S0 = true;
      std::cout << "  SUCCESS: Found Track 0, Sector 0!" << std::endl;
      break;
    }

    attempts++;
  }

  std::cout << std::endl;

  if (!foundT0S0)
  {
    std::cerr << "ERROR: Could not find Track 0, Sector 0 after " << attempts << " sectors!" << std::endl;
    std::cerr << "The boot ROM would fail at this point." << std::endl;
    return 1;
  }

  // Step 9: Test seeking to and reading from multiple tracks
  // DOS 3.3 needs to access tracks across the entire disk, so we'll test several
  std::cout << "Step 9: Testing multi-track seeking and reading..." << std::endl;
  std::cout << "  (DOS 3.3 boot process needs to read from various tracks)" << std::endl;
  std::cout << std::endl;

  // Test these tracks: 1, 2, 5, 10, 17 (covers range of typical DOS 3.3 usage)
  std::vector<int> test_tracks = {1, 2, 5, 10, 17};
  int current_physical_track = 0;  // We're on track 0 now
  int current_phase = 0;  // Track the current stepper motor phase
  bool all_tracks_ok = true;

  for (int target_track : test_tracks)
  {
    std::cout << "  Testing Track " << target_track << ":" << std::endl;

    // Seek from current track to target track
    // Each phase step = 1/2 track, so we need 2 steps per track
    int tracks_to_move = target_track - current_physical_track;
    int half_tracks_to_move = tracks_to_move * 2;

    if (half_tracks_to_move > 0)
    {
      // Seek outward (increase track number)
      for (int step = 0; step < half_tracks_to_move; step++)
      {
        int next_phase = (current_phase + 1) % 4;

        // Turn on next phase
        diskII.write(0xC0E0 + (next_phase * 2) + 1, 0);
        cycle += 1000;

        // Turn off current phase
        diskII.write(0xC0E0 + (current_phase * 2), 0);
        cycle += 1000;

        current_phase = next_phase;
      }
    }
    else if (half_tracks_to_move < 0)
    {
      // Seek inward (decrease track number)
      for (int step = 0; step < -half_tracks_to_move; step++)
      {
        int prev_phase = (current_phase - 1 + 4) % 4;

        // Turn on previous phase
        diskII.write(0xC0E0 + (prev_phase * 2) + 1, 0);
        cycle += 1000;

        // Turn off current phase
        diskII.write(0xC0E0 + (current_phase * 2), 0);
        cycle += 1000;

        current_phase = prev_phase;
      }
    }

    current_physical_track = target_track;

    std::cout << "    Seeked to physical track " << diskII.getCurrentTrack() << std::endl;

    // Try to read a sector from this track
    if (!findSequence(diskII, cycle, ADDRESS_PROLOGUE, 3, 5000))
    {
      std::cerr << "    ✗ ERROR: Could not find address prologue on Track " << target_track << std::endl;
      all_tracks_ok = false;
      continue;
    }

    // Read address field to verify we're on the right track
    uint8_t vol = decode4and4(diskII, cycle);
    uint8_t trk = decode4and4(diskII, cycle);
    uint8_t sec = decode4and4(diskII, cycle);
    uint8_t chk = decode4and4(diskII, cycle);

    if (trk == target_track)
    {
      std::cout << "    ✓ Found sector on Track " << static_cast<int>(trk)
                << ", Sector " << static_cast<int>(sec) << std::endl;
    }
    else
    {
      std::cerr << "    ✗ ERROR: Expected Track " << target_track
                << " but found Track " << static_cast<int>(trk) << std::endl;
      all_tracks_ok = false;
    }
  }

  std::cout << std::endl;

  // Summary
  std::cout << "╔════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║              BOOT SIMULATION COMPLETE          ║" << std::endl;
  std::cout << "╚════════════════════════════════════════════════╝" << std::endl;
  std::cout << std::endl;

  if (all_tracks_ok)
  {
    std::cout << "✓ SUCCESS: All boot ROM operations completed!" << std::endl;
    std::cout << "✓ Disk is readable across multiple tracks" << std::endl;
    std::cout << "✓ Track seeking works correctly" << std::endl;
    std::cout << "✓ The boot ROM should be able to boot from this disk" << std::endl;
    return 0;
  }
  else
  {
    std::cout << "✗ ERROR: Some tracks could not be read correctly" << std::endl;
    std::cout << "✗ DOS 3.3 boot may fail when loading files" << std::endl;
    return 1;
  }
}
