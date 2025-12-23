#include "ui/window_manager.hpp"
#include "emulator/emulator.hpp"
#include "preferences.hpp"

window_manager::window_manager() = default;

window_manager::~window_manager() = default;

void window_manager::initialize(emulator& emu, void* metal_device)
{
  // Initialize video texture with Metal device (needed before creating video window)
  if (metal_device)
  {
    emu.initializeVideoTexture(metal_device);
  }

  // Create CPU window
  auto cpu_win = std::make_unique<cpu_window>(emu);
  cpu_win->setOpen(true);
  cpu_window_ = cpu_win.get();
  windows_.push_back(std::move(cpu_win));

  // Create memory viewer window
  auto mem_win = std::make_unique<memory_viewer_window>(emu);
  mem_win->setOpen(true);
  memory_viewer_window_ = mem_win.get();
  windows_.push_back(std::move(mem_win));

  // Create video window
  auto vid_win = std::make_unique<video_window>(emu);
  vid_win->setOpen(true);
  video_window_ = vid_win.get();
  windows_.push_back(std::move(vid_win));

  // Create soft switches window
  auto ss_win = std::make_unique<soft_switches_window>(emu);
  ss_win->setOpen(false);  // Start closed by default
  soft_switches_window_ = ss_win.get();
  windows_.push_back(std::move(ss_win));

  // Create disk window
  auto disk_win = std::make_unique<disk_window>(emu);
  disk_win->setOpen(false);  // Start closed by default
  disk_window_ = disk_win.get();
  windows_.push_back(std::move(disk_win));
}

void window_manager::update(float deltaTime)
{
  for (auto& window : windows_)
  {
    window->update(deltaTime);
  }
}

void window_manager::render()
{
  for (auto& window : windows_)
  {
    window->render();
  }
}

void window_manager::loadState(preferences& prefs)
{
  if (cpu_window_)
  {
    cpu_window_->setOpen(prefs.getBool("window.cpu.visible", true));
  }

  if (memory_viewer_window_)
  {
    memory_viewer_window_->setOpen(prefs.getBool("window.memory_viewer.visible", true));
  }

  if (video_window_)
  {
    video_window_->setOpen(prefs.getBool("window.video.visible", true));
  }

  if (soft_switches_window_)
  {
    soft_switches_window_->setOpen(prefs.getBool("window.soft_switches.visible", false));
  }

  if (disk_window_)
  {
    disk_window_->setOpen(prefs.getBool("window.disk.visible", false));
  }
}

void window_manager::saveState(preferences& prefs)
{
  if (cpu_window_)
  {
    prefs.setBool("window.cpu.visible", cpu_window_->isOpen());
  }

  if (memory_viewer_window_)
  {
    prefs.setBool("window.memory_viewer.visible", memory_viewer_window_->isOpen());
  }

  if (video_window_)
  {
    prefs.setBool("window.video.visible", video_window_->isOpen());
  }

  if (soft_switches_window_)
  {
    prefs.setBool("window.soft_switches.visible", soft_switches_window_->isOpen());
  }

  if (disk_window_)
  {
    prefs.setBool("window.disk.visible", disk_window_->isOpen());
  }
}
