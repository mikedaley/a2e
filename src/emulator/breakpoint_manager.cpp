#include "emulator/breakpoint_manager.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

void breakpoint_manager::addBreakpoint(uint16_t address, breakpoint_type type)
{
  // Check if already exists
  if (hasBreakpoint(address, type))
  {
    return;
  }

  breakpoint bp;
  bp.address = address;
  bp.type = type;
  bp.enabled = true;

  size_t index = breakpoints_.size();
  breakpoints_.push_back(bp);

  // Add to appropriate map
  switch (type)
  {
  case breakpoint_type::EXECUTION:
    execution_map_[address].push_back(index);
    break;
  case breakpoint_type::READ:
    read_map_[address].push_back(index);
    break;
  case breakpoint_type::WRITE:
    write_map_[address].push_back(index);
    break;
  }
}

void breakpoint_manager::removeBreakpoint(uint16_t address, breakpoint_type type)
{
  size_t index = findBreakpoint(address, type);
  if (index == static_cast<size_t>(-1))
  {
    return;
  }

  breakpoints_.erase(breakpoints_.begin() + index);
  rebuildMaps();
}

void breakpoint_manager::toggleBreakpoint(uint16_t address, breakpoint_type type)
{
  if (hasBreakpoint(address, type))
  {
    removeBreakpoint(address, type);
  }
  else
  {
    addBreakpoint(address, type);
  }
}

bool breakpoint_manager::hasBreakpoint(uint16_t address, breakpoint_type type) const
{
  return findBreakpoint(address, type) != static_cast<size_t>(-1);
}

void breakpoint_manager::setEnabled(uint16_t address, breakpoint_type type, bool enabled)
{
  size_t index = findBreakpoint(address, type);
  if (index != static_cast<size_t>(-1))
  {
    breakpoints_[index].enabled = enabled;
  }
}

bool breakpoint_manager::checkExecution(uint16_t pc) const
{
  auto it = execution_map_.find(pc);
  if (it == execution_map_.end())
  {
    return false;
  }

  for (size_t index : it->second)
  {
    if (breakpoints_[index].enabled)
    {
      return true;
    }
  }
  return false;
}

bool breakpoint_manager::checkRead(uint16_t address) const
{
  auto it = read_map_.find(address);
  if (it == read_map_.end())
  {
    return false;
  }

  for (size_t index : it->second)
  {
    if (breakpoints_[index].enabled)
    {
      return true;
    }
  }
  return false;
}

bool breakpoint_manager::checkWrite(uint16_t address) const
{
  auto it = write_map_.find(address);
  if (it == write_map_.end())
  {
    return false;
  }

  for (size_t index : it->second)
  {
    if (breakpoints_[index].enabled)
    {
      return true;
    }
  }
  return false;
}

std::string breakpoint_manager::serialize() const
{
  std::ostringstream oss;
  bool first = true;

  for (const auto& bp : breakpoints_)
  {
    if (!first)
    {
      oss << ";";
    }
    first = false;

    oss << std::hex << std::setw(4) << std::setfill('0') << bp.address << ":"
        << static_cast<int>(bp.type) << ":"
        << (bp.enabled ? "1" : "0");
  }

  return oss.str();
}

void breakpoint_manager::deserialize(const std::string& data)
{
  clear();

  if (data.empty())
  {
    return;
  }

  std::istringstream iss(data);
  std::string token;

  while (std::getline(iss, token, ';'))
  {
    std::istringstream token_stream(token);
    std::string addr_str, type_str, enabled_str;

    if (!std::getline(token_stream, addr_str, ':') ||
        !std::getline(token_stream, type_str, ':') ||
        !std::getline(token_stream, enabled_str, ':'))
    {
      continue;
    }

    try
    {
      uint16_t address = static_cast<uint16_t>(std::stoul(addr_str, nullptr, 16));
      int type_int = std::stoi(type_str);
      bool enabled = (enabled_str == "1");

      breakpoint bp;
      bp.address = address;
      bp.type = static_cast<breakpoint_type>(type_int);
      bp.enabled = enabled;

      breakpoints_.push_back(bp);
    }
    catch (...)
    {
      // Skip malformed entries
      continue;
    }
  }

  rebuildMaps();
}

void breakpoint_manager::clear()
{
  breakpoints_.clear();
  execution_map_.clear();
  read_map_.clear();
  write_map_.clear();
}

void breakpoint_manager::rebuildMaps()
{
  execution_map_.clear();
  read_map_.clear();
  write_map_.clear();

  for (size_t i = 0; i < breakpoints_.size(); ++i)
  {
    const auto& bp = breakpoints_[i];

    switch (bp.type)
    {
    case breakpoint_type::EXECUTION:
      execution_map_[bp.address].push_back(i);
      break;
    case breakpoint_type::READ:
      read_map_[bp.address].push_back(i);
      break;
    case breakpoint_type::WRITE:
      write_map_[bp.address].push_back(i);
      break;
    }
  }
}

size_t breakpoint_manager::findBreakpoint(uint16_t address, breakpoint_type type) const
{
  for (size_t i = 0; i < breakpoints_.size(); ++i)
  {
    if (breakpoints_[i].address == address && breakpoints_[i].type == type)
    {
      return i;
    }
  }
  return static_cast<size_t>(-1);
}
