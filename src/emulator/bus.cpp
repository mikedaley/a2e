#include "emulator/bus.hpp"
#include <algorithm>

Bus::Bus() = default;

Bus::~Bus() = default;

void Bus::registerDevice(std::unique_ptr<Device> device)
{
  if (device)
  {
    devices_.push_back(std::move(device));
  }
}

bool Bus::unregisterDevice(const std::string &name)
{
  auto it = std::find_if(devices_.begin(), devices_.end(),
                         [&name](const std::unique_ptr<Device> &device)
                         {
                           return device->getName() == name;
                         });

  if (it != devices_.end())
  {
    devices_.erase(it);
    return true;
  }
  return false;
}

uint8_t Bus::read(uint16_t address) const
{
  Device *device = findDevice(address);
  if (device)
  {
    return device->read(address);
  }
  // Return 0xFF (uninitialized memory) if no device handles this address
  return 0xFF;
}

void Bus::write(uint16_t address, uint8_t value) const
{
  Device *device = findDevice(address);
  if (device)
  {
    device->write(address, value);
  }
  // Silently ignore writes to unmapped addresses
}

size_t Bus::getDeviceCount() const
{
  return devices_.size();
}

Device *Bus::getDevice(const std::string &name) const
{
  auto it = std::find_if(devices_.begin(), devices_.end(),
                         [&name](const std::unique_ptr<Device> &device)
                         {
                           return device->getName() == name;
                         });

  if (it != devices_.end())
  {
    return it->get();
  }
  return nullptr;
}

Device *Bus::findDevice(uint16_t address) const
{
  // Search in reverse order so last registered device wins (priority-based)
  for (auto it = devices_.rbegin(); it != devices_.rend(); ++it)
  {
    const auto &device = *it;
    if (device->getAddressRange().contains(address))
    {
      return device.get();
    }
  }
  return nullptr;
}

