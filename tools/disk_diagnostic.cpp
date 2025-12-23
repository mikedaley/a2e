#include "emulator/disk_ii.hpp"
#include "emulator/disk_image.hpp"
#include <iostream>
#include <iomanip>

/**
 * Diagnostic tool to test Disk II timing and data latch behavior
 */
int main()
{
  std::cout << "===========================================================" << std::endl;
  std::cout << "Disk II Diagnostic Test" << std::endl;
  std::cout << "===========================================================" << std::endl;
  std::cout << std::endl;

  // Create a Disk II controller
  DiskII diskII(6);

  // Insert our test disk
  if (!diskII.insertDisk(0, "resources/roms/disk/test_disk.dsk"))
  {
    std::cerr << "ERROR: Could not insert test disk" << std::endl;
    return 1;
  }

  std::cout << "Test disk inserted successfully" << std::endl;
  std::cout << std::endl;

  // Simulate the boot ROM sequence
  std::cout << "Simulating Apple II boot ROM disk read sequence..." << std::endl;
  std::cout << "===========================================================" << std::endl;
  std::cout << std::endl;

  uint64_t cycle = 0;

  // Turn on motor (soft switch $C089)
  std::cout << "Step 1: Turn on motor ($C089)" << std::endl;
  diskII.update(cycle);
  diskII.write(0xC0E9, 0);  // MOTOR_ON
  cycle += 10;
  std::cout << std::endl;

  // Set read mode ($C08E - Q7L)
  std::cout << "Step 2: Set read mode ($C08E - Q7L)" << std::endl;
  diskII.update(cycle);
  diskII.read(0xC0EE);  // Q7L
  cycle += 10;
  std::cout << std::endl;

  // Read data latch multiple times ($C08C - Q6L)
  std::cout << "Step 3: Read data latch 20 times with varying cycle gaps" << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;

  for (int i = 0; i < 20; i++)
  {
    // Vary the cycle gap to simulate different instruction timing
    if (i < 5)
    {
      cycle += 10;  // Very fast reads (won't advance much)
    }
    else if (i < 10)
    {
      cycle += 32;  // Exactly 1 byte worth of cycles
    }
    else if (i < 15)
    {
      cycle += 64;  // 2 bytes worth
    }
    else
    {
      cycle += 128;  // 4 bytes worth
    }

    diskII.update(cycle);
    uint8_t value = diskII.read(0xC0EC);  // Q6L - read data latch

    std::cout << "Read #" << std::setw(2) << i
              << " | Cycle=" << std::setw(6) << cycle
              << " | Value=$" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value)
              << std::dec << " | Track=" << diskII.getCurrentTrack()
              << std::endl;
  }

  std::cout << std::endl;

  // Step 4: Test track seeking (stepper motor)
  std::cout << "Step 4: Test stepper motor - seek to different tracks" << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;

  // Seek to track 1 (move forward 1 track = 2 half-tracks)
  std::cout << "\nSeeking to Track 1..." << std::endl;

  // Turn on phase 1 (currently on phase 0)
  diskII.write(0xC0E3, 0);  // PHASE1_ON
  cycle += 100;
  std::cout << "Current track after PHASE1_ON: " << diskII.getCurrentTrack() << std::endl;

  // Turn off phase 0
  diskII.write(0xC0E0, 0);  // PHASE0_OFF
  cycle += 100;
  std::cout << "Current track after PHASE0_OFF: " << diskII.getCurrentTrack() << std::endl;

  // Seek to track 2
  std::cout << "\nSeeking to Track 2..." << std::endl;

  // Turn on phase 2
  diskII.write(0xC0E5, 0);  // PHASE2_ON
  cycle += 100;
  std::cout << "Current track after PHASE2_ON: " << diskII.getCurrentTrack() << std::endl;

  // Turn off phase 1
  diskII.write(0xC0E2, 0);  // PHASE1_OFF
  cycle += 100;
  std::cout << "Current track after PHASE1_OFF: " << diskII.getCurrentTrack() << std::endl;

  // Try reading some data from track 2
  std::cout << "\nReading data from Track 2..." << std::endl;
  for (int i = 0; i < 5; i++)
  {
    cycle += 64;
    diskII.update(cycle);
    uint8_t value = diskII.read(0xC0EC);
    std::cout << "Read " << i << ": $" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(value) << std::dec << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Diagnostic complete!" << std::endl;
  std::cout << std::endl;
  std::cout << "Expected behavior:" << std::endl;
  std::cout << "- First read after motor on should return first byte of nibblized track" << std::endl;
  std::cout << "- Subsequent reads should advance through the track based on cycles elapsed" << std::endl;
  std::cout << "- Each 32 cycles = 1 byte advancement" << std::endl;
  std::cout << "- Stepper motor should move head between tracks when phases change" << std::endl;
  std::cout << "- Data from different tracks should show different track numbers in nibblized data" << std::endl;

  return 0;
}
