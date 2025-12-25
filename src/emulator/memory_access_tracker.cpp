#include "emulator/memory_access_tracker.hpp"

void memory_access_tracker::recordRead(uint16_t address)
{
  if (!enabled_)
    return;

  entries_[address].type = access_type::READ;
  entries_[address].fade_timer = FADE_DURATION;
}

void memory_access_tracker::recordWrite(uint16_t address)
{
  if (!enabled_)
    return;

  // Last access wins - write overwrites read
  entries_[address].type = access_type::WRITE;
  entries_[address].fade_timer = FADE_DURATION;
}

void memory_access_tracker::update(float deltaTime)
{
  if (!enabled_)
    return;

  for (auto &entry : entries_)
  {
    if (entry.fade_timer > 0.0f)
    {
      entry.fade_timer -= deltaTime;
      if (entry.fade_timer <= 0.0f)
      {
        entry.fade_timer = 0.0f;
        entry.type = access_type::NONE;
      }
    }
  }
}

const memory_access_entry &memory_access_tracker::getEntry(uint16_t address) const
{
  return entries_[address];
}

void memory_access_tracker::setEnabled(bool enabled)
{
  enabled_ = enabled;
}

bool memory_access_tracker::isEnabled() const
{
  return enabled_;
}

void memory_access_tracker::clear()
{
  for (auto &entry : entries_)
  {
    entry.type = access_type::NONE;
    entry.fade_timer = 0.0f;
  }
}
