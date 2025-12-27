#include "emulator/disk2_controller.hpp"
#include "emulator/disk_formats/woz_disk_image.hpp"
#include "emulator/disk_formats/dsk_disk_image.hpp"
#include "utils/resource_path.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstdlib>

Disk2Controller::Disk2Controller()
{
  // Initialize slot ROM to 0xFF (unprogrammed state)
  slot_rom_.fill(0xFF);
}

void Disk2Controller::reset()
{
  // Reset controller to power-on state
  motor_on_ = false;
  motor_off_cycle_ = 0;
  selected_drive_ = 0;
  q6_ = false;
  q7_ = false;
  phase_states_ = 0;

  // Reset timing state
  last_read_cycle_[0] = 0;
  last_read_cycle_[1] = 0;
  last_write_cycle_[0] = 0;
  last_write_cycle_[1] = 0;
  data_latch_ = 0;
  latch_valid_ = false;

  // Reset write state
  write_latch_ = 0;
  write_pending_ = false;
}

bool Disk2Controller::initialize()
{
  std::cout << "Initializing Disk II Controller (Slot 6)..." << std::endl;

  if (!loadControllerROM())
  {
    std::cerr << "Warning: Failed to load Disk II controller ROM" << std::endl;
    return false;
  }

  std::cout << "Disk II Controller initialized" << std::endl;
  return true;
}

bool Disk2Controller::loadControllerROM()
{
  // Load the P5 ROM (341-0027) - 256 bytes
  const char *rom_path = "resources/roms/disk/341-0027.bin";
  std::string fullPath = getResourcePath(rom_path);

  std::ifstream file(fullPath, std::ios::binary);
  if (!file.is_open())
  {
    std::cerr << "Failed to open Disk II ROM: " << rom_path
              << " (tried path: " << fullPath << ")" << std::endl;
    return false;
  }

  // Check file size
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size != ROM_SIZE)
  {
    std::cerr << "Disk II ROM size mismatch: expected " << ROM_SIZE
              << " bytes, got " << file_size << std::endl;
    file.close();
    return false;
  }

  // Read ROM data
  file.read(reinterpret_cast<char *>(slot_rom_.data()), ROM_SIZE);
  file.close();

  rom_loaded_ = true;
  std::cout << "Loaded Disk II controller ROM (341-0027) at $C600-$C6FF" << std::endl;

  return true;
}

uint8_t Disk2Controller::read(uint16_t address)
{
  // Handle slot ROM reads ($C600-$C6FF)
  if (isSlotROMAddress(address))
  {
    return readSlotROM(address);
  }

  // Handle I/O space reads ($C0E0-$C0EF)
  if (address >= IO_BASE && address <= IO_END)
  {
    uint8_t offset = address - IO_BASE;
    return handleSoftSwitch(offset, false);
  }

  return 0xFF;
}

void Disk2Controller::write(uint16_t address, uint8_t value)
{
  // Handle I/O space writes ($C0E0-$C0EF)
  if (address >= IO_BASE && address <= IO_END)
  {
    uint8_t offset = address - IO_BASE;

    // In write mode, writing to Q6H (or Q6L) loads data into the shift register
    // and immediately begins shifting it out. We write the nibble directly here
    // rather than waiting for a separate shift trigger.
    // The Disk II hardware automatically shifts bits based on timing; the CPU
    // just needs to load new bytes before the shift register empties.
    if (q7_ && (offset == Q6H || offset == Q6L))
    {
      write_latch_ = value;
      // Write immediately - don't wait for Q6L access
      writeDiskData();
    }

    handleSoftSwitch(offset, true);
  }
  // Writes to ROM space are ignored
}

AddressRange Disk2Controller::getAddressRange() const
{
  // Return I/O address range
  // Note: Slot ROM ($C600-$C6FF) is handled separately via isSlotROMAddress
  return {IO_BASE, IO_END};
}

std::string Disk2Controller::getName() const
{
  return "Disk II Controller";
}

bool Disk2Controller::isSlotROMAddress(uint16_t address) const
{
  return address >= ROM_BASE && address <= ROM_END;
}

uint8_t Disk2Controller::readSlotROM(uint16_t address) const
{
  if (!rom_loaded_)
  {
    return 0xFF;
  }

  uint16_t offset = address - ROM_BASE;
  if (offset < ROM_SIZE)
  {
    return slot_rom_[offset];
  }

  return 0xFF;
}

void Disk2Controller::setCycleCallback(CycleCountCallback callback)
{
  cycle_callback_ = std::move(callback);
}

uint64_t Disk2Controller::getCycles() const
{
  if (cycle_callback_)
  {
    return cycle_callback_();
  }
  return 0;
}

uint8_t Disk2Controller::handleSoftSwitch(uint8_t offset, bool is_write)
{
  (void)is_write; // Both reads and writes toggle/access the switches

  // Helper to forward phase changes to disk image
  auto setPhase = [this](int phase, bool on)
  {
    if (hasDisk(selected_drive_))
    {
      disk_images_[selected_drive_]->setPhase(phase, on);
    }
  };

  switch (offset)
  {
  case PHASE0_OFF:
    phase_states_ &= ~0x01;
    setPhase(0, false);
    break;
  case PHASE0_ON:
    phase_states_ |= 0x01;
    setPhase(0, true);
    break;
  case PHASE1_OFF:
    phase_states_ &= ~0x02;
    setPhase(1, false);
    break;
  case PHASE1_ON:
    phase_states_ |= 0x02;
    setPhase(1, true);
    break;
  case PHASE2_OFF:
    phase_states_ &= ~0x04;
    setPhase(2, false);
    break;
  case PHASE2_ON:
    phase_states_ |= 0x04;
    setPhase(2, true);
    break;
  case PHASE3_OFF:
    phase_states_ &= ~0x08;
    setPhase(3, false);
    break;
  case PHASE3_ON:
    phase_states_ |= 0x08;
    setPhase(3, true);
    break;

  case MOTOR_OFF:
    // Start the motor-off delay timer (motor stays on for ~1 second)
    if (motor_on_ && motor_off_cycle_ == 0)
    {
      motor_off_cycle_ = getCycles();
    }
    break;
  case MOTOR_ON:
    // Cancel any pending motor-off and turn motor on
    motor_off_cycle_ = 0;
    motor_on_ = true;
    break;

  case DRIVE1_SELECT:
    selected_drive_ = 0;
    break;
  case DRIVE2_SELECT:
    selected_drive_ = 1;
    break;

  case Q6L:
    q6_ = false;
    if (!q7_)
    {
      // Read mode (Q7=0, Q6=0): return data from disk
      return readDiskData();
    }
    // In write mode, Q6L access is just for timing/status
    // Actual writes happen immediately when data is stored to Q6H
    break;

  case Q6H:
    q6_ = true;
    // In read mode (Q7=0, Q6=1): return write protect status
    // Bit 7 = 1 means write protected
    if (!q7_)
    {
      // Check if current drive has a write-protected disk
      if (hasDisk(selected_drive_))
      {
        const DiskImage *disk = disk_images_[selected_drive_].get();
        return disk->isWriteProtected() ? 0x80 : 0x00;
      }
      return 0x80; // No disk = write protected
    }
    break;

  case Q7L:
    q7_ = false; // Read mode
    write_pending_ = false; // Cancel any pending write
    // Reset read timing so first read after write mode gets fresh data
    last_read_cycle_[selected_drive_] = 0;
    latch_valid_ = false;
    break;

  case Q7H:
    q7_ = true; // Write mode
    break;
  }

  // Default return value for reads
  return 0x00;
}

// ===== Disk operations =====

bool Disk2Controller::insertDisk(int drive, const std::string &filename)
{
  if (drive < 0 || drive > 1)
  {
    std::cerr << "Invalid drive number: " << drive << std::endl;
    return false;
  }

  // Determine file type from extension
  std::string lower_filename = filename;
  std::transform(lower_filename.begin(), lower_filename.end(),
                 lower_filename.begin(), ::tolower);

  std::unique_ptr<DiskImage> image;

  if (lower_filename.ends_with(".woz"))
  {
    image = std::make_unique<WozDiskImage>();
  }
  else if (lower_filename.ends_with(".dsk") ||
           lower_filename.ends_with(".do") ||
           lower_filename.ends_with(".po"))
  {
    image = std::make_unique<DskDiskImage>();
  }
  else
  {
    std::cerr << "Unsupported disk image format: " << filename << std::endl;
    return false;
  }

  if (!image->load(filename))
  {
    std::cerr << "Failed to load disk image: " << filename << std::endl;
    return false;
  }

  disk_images_[drive] = std::move(image);
  std::cout << "Inserted disk into drive " << (drive + 1) << ": " << filename
            << " (" << disk_images_[drive]->getFormatName() << ")" << std::endl;

  return true;
}

void Disk2Controller::ejectDisk(int drive)
{
  if (drive < 0 || drive > 1)
  {
    return;
  }

  if (disk_images_[drive])
  {
    std::cout << "Ejecting disk from drive " << (drive + 1) << "..." << std::endl;

    // Only turn off motor if this is the currently selected drive
    // (don't disrupt the other drive if it's active)
    if (selected_drive_ == drive)
    {
      motor_on_ = false;
      motor_off_cycle_ = 0;
    }

    // Save any modifications before ejecting
    std::cout << "Saving disk image..." << std::endl;
    bool saved = disk_images_[drive]->save();
    if (saved)
    {
      std::cout << "Saved disk in drive " << (drive + 1) << std::endl;
    }
    else
    {
      std::cerr << "Warning: Failed to save disk in drive " << (drive + 1) << std::endl;
    }

    std::cout << "Releasing disk image..." << std::endl;
    disk_images_[drive].reset();
    std::cout << "Ejected disk from drive " << (drive + 1) << std::endl;
  }
}

void Disk2Controller::saveAllDisks()
{
  for (int drive = 0; drive < 2; drive++)
  {
    if (disk_images_[drive])
    {
      if (disk_images_[drive]->save())
      {
        std::cout << "Saved disk in drive " << (drive + 1) << std::endl;
      }
    }
  }
}

bool Disk2Controller::hasDisk(int drive) const
{
  if (drive < 0 || drive > 1)
  {
    return false;
  }
  return disk_images_[drive] != nullptr && disk_images_[drive]->isLoaded();
}

const DiskImage *Disk2Controller::getDiskImage(int drive) const
{
  if (drive < 0 || drive > 1)
  {
    return nullptr;
  }
  return disk_images_[drive].get();
}

bool Disk2Controller::isMotorOn() const
{
  // Check if motor-off delay has elapsed
  if (motor_on_ && motor_off_cycle_ != 0)
  {
    uint64_t current = getCycles();
    if (current >= motor_off_cycle_ + MOTOR_OFF_DELAY_CYCLES)
    {
      // Delay has elapsed, actually turn off the motor
      motor_on_ = false;
      motor_off_cycle_ = 0;
    }
  }
  return motor_on_;
}

int Disk2Controller::getSelectedDrive() const
{
  return selected_drive_;
}

uint8_t Disk2Controller::getPhaseStates() const
{
  return phase_states_;
}

int Disk2Controller::getCurrentTrack() const
{
  if (hasDisk(selected_drive_))
  {
    return disk_images_[selected_drive_]->getTrack();
  }
  return -1;
}

int Disk2Controller::getQuarterTrack() const
{
  if (hasDisk(selected_drive_))
  {
    return disk_images_[selected_drive_]->getQuarterTrack();
  }
  return -1;
}

uint8_t Disk2Controller::readDiskData()
{
  // If motor is off or no disk, return 0
  if (!isMotorOn() || !hasDisk(selected_drive_))
  {
    return 0;
  }

  DiskImage *disk = disk_images_[selected_drive_].get();

  // Check if current head position has data
  if (!disk->hasData())
  {
    return 0;
  }

  // The Disk II hardware shifts bits into a latch. When bit 7 becomes set,
  // we have a valid nibble. The boot ROM polls Q6L in a loop:
  //   LDY $C08C,X  ; 4 cycles
  //   BPL loop     ; 2/3 cycles
  // It expects bit 7 to be CLEAR between nibbles. After reading a valid
  // nibble (~32 cycles to shift in), the ROM processes it, then polls again.
  // If we return the same nibble with bit 7 still set, the ROM will process
  // it twice! We must clear bit 7 after the first read until the next nibble.

  uint64_t current_cycle = getCycles();
  uint64_t &last_cycle = last_read_cycle_[selected_drive_];
  static constexpr uint64_t CYCLES_PER_NIBBLE = 32;

  // Check if enough time has passed for a new nibble
  bool new_nibble_ready = (last_cycle == 0) ||
                          (current_cycle >= last_cycle + CYCLES_PER_NIBBLE);

  if (new_nibble_ready)
  {
    // Read new nibble from disk
    data_latch_ = disk->readNibble();
    last_cycle = current_cycle;
    latch_valid_ = true;  // This nibble hasn't been read yet

  }

  // Return the nibble. If this is a repeat read before next nibble is ready,
  // clear bit 7 so the ROM's BPL loop will wait.
  if (latch_valid_)
  {
    latch_valid_ = false;  // Mark as read
    return data_latch_;    // Return with bit 7 set
  }
  else
  {
    // Already read this nibble, return with bit 7 clear
    return data_latch_ & 0x7F;
  }
}

void Disk2Controller::writeDiskData()
{
  // If motor is off, no disk, or not in write mode, do nothing
  if (!isMotorOn() || !hasDisk(selected_drive_) || !q7_)
  {
    static int skip_count = 0;
    if (++skip_count <= 10)
      std::cerr << "Write skipped: motor=" << isMotorOn()
                << " hasDisk=" << hasDisk(selected_drive_)
                << " q7=" << q7_ << std::endl;
    return;
  }

  DiskImage *disk = disk_images_[selected_drive_].get();

  // Check write protection
  if (disk->isWriteProtected())
  {
    static int wp_count = 0;
    if (++wp_count <= 10)
      std::cerr << "Write skipped: disk is write protected" << std::endl;
    return;
  }

  // Check if current head position has data
  if (!disk->hasData())
  {
    static int nodata_count = 0;
    if (++nodata_count <= 10)
      std::cerr << "Write skipped: no data at quarter-track "
                << disk->getQuarterTrack() << std::endl;
    return;
  }

  // Track timing for diagnostics
  // Real hardware writes regardless of timing; the data just may be slightly offset
  uint64_t current_cycle = getCycles();
  uint64_t &last_cycle = last_write_cycle_[selected_drive_];

  // Always write the nibble - timing issues shouldn't drop data
  disk->writeNibble(write_latch_);
  last_cycle = current_cycle;
}
