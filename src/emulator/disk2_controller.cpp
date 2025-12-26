#include "emulator/disk2_controller.hpp"
#include "emulator/disk_formats/woz_disk_image.hpp"
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
  selected_drive_ = 0;
  q6_ = false;
  q7_ = false;
  phase_states_ = 0;
  // Random track position simulates unknown head position at power-on
  half_track_ = rand() % 70;
  last_phase_ = 0;
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
  (void)value; // Value is typically ignored for soft switches

  // Handle I/O space writes ($C0E0-$C0EF)
  if (address >= IO_BASE && address <= IO_END)
  {
    uint8_t offset = address - IO_BASE;
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

  switch (offset)
  {
  case PHASE0_OFF:
    phase_states_ &= ~0x01;
    break;
  case PHASE0_ON:
    phase_states_ |= 0x01;
    updateHeadPosition(0);
    break;
  case PHASE1_OFF:
    phase_states_ &= ~0x02;
    break;
  case PHASE1_ON:
    phase_states_ |= 0x02;
    updateHeadPosition(1);
    break;
  case PHASE2_OFF:
    phase_states_ &= ~0x04;
    break;
  case PHASE2_ON:
    phase_states_ |= 0x04;
    updateHeadPosition(2);
    break;
  case PHASE3_OFF:
    phase_states_ &= ~0x08;
    break;
  case PHASE3_ON:
    phase_states_ |= 0x08;
    updateHeadPosition(3);
    break;

  case MOTOR_OFF:
    motor_on_ = false;
    break;
  case MOTOR_ON:
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
    // In read mode (Q7=0, Q6=0): return data from disk
    // For now, return 0x00 (no disk / sync byte)
    if (!q7_)
    {
      return 0x00; // Stub: would return disk data
    }
    break;

  case Q6H:
    q6_ = true;
    // In read mode (Q7=0, Q6=1): return write protect status
    // Bit 7 = 1 means write protected
    if (!q7_)
    {
      return 0x80; // Stub: disk is write protected
    }
    break;

  case Q7L:
    q7_ = false; // Read mode
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
    std::cout << "Ejected disk from drive " << (drive + 1) << std::endl;
    disk_images_[drive].reset();
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
  return half_track_ / 2;
}

int Disk2Controller::getHalfTrack() const
{
  return half_track_;
}

void Disk2Controller::updateHeadPosition(int phase)
{
  // The Disk II uses a 4-phase stepper motor
  // Phases are activated in sequence: 0-1-2-3-0-1-2-3 for inward movement
  // and 3-2-1-0-3-2-1-0 for outward movement
  // Each phase change moves the head by one half-track

  // Calculate the phase difference to determine direction
  // Phase sequence for stepping in:  0 -> 1 -> 2 -> 3 -> 0  (clockwise)
  // Phase sequence for stepping out: 0 -> 3 -> 2 -> 1 -> 0  (counter-clockwise)

  int phase_diff = phase - last_phase_;

  // Normalize to handle wrap-around (e.g., 3 -> 0 is +1, 0 -> 3 is -1)
  if (phase_diff == 3) phase_diff = -1;
  if (phase_diff == -3) phase_diff = 1;

  // Only move if the phase change is a valid single step (+1 or -1)
  if (phase_diff == 1)
  {
    // Stepping inward (toward higher track numbers)
    if (half_track_ < 69)  // Max is track 34.5 (half-track 69)
    {
      half_track_++;
    }
  }
  else if (phase_diff == -1)
  {
    // Stepping outward (toward track 0)
    if (half_track_ > 0)
    {
      half_track_--;
    }
  }
  // If phase_diff is 0, 2, or -2, it's not a valid step (head doesn't move)

  last_phase_ = phase;
}
