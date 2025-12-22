#include "application.hpp"
#include <imgui.h>
#include <imgui_impl_metal_custom.h>
#include <iostream>

application::application()
{
  // Initialize preferences
  preferences_ = std::make_unique<preferences>("a2e");
  preferences_->load();
}

application::~application()
{
  // Save window state before shutdown
  saveWindowState();
}

bool application::initialize()
{
  try
  {
    // Create and initialize the emulator
    emulator_ = std::make_unique<emulator>();
    if (!emulator_->initialize())
    {
      std::cerr << "Failed to initialize emulator" << std::endl;
      return false;
    }

    // Configure window renderer
    window_renderer::config config;
    config.title = "Apple IIe Emulator - Memory Viewer";
    config.width = 1280;
    config.height = 800;
    config.vsync = true;
    config.docking = true;
    config.viewports = false;

    // Create window renderer
    window_renderer_ = std::make_unique<window_renderer>(config);

    std::cout << "Application initialization complete!" << std::endl;
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Application initialization failed: " << e.what() << std::endl;
    return false;
  }
}

int application::run()
{
  if (!window_renderer_)
  {
    std::cerr << "Application not initialized" << std::endl;
    return 1;
  }

  // Setup UI
  setupUI();

  // Run the main loop
  return window_renderer_->run(
      [this]()
      { renderUI(); },
      [this](float deltaTime)
      { update(deltaTime); });
}

void application::setupUI()
{
  // Create windows
  cpu_window_ = std::make_unique<cpu_window>();
  cpu_window_->setOpen(true);

  // Set memory read callback for CPU window (for stack/disassembly display)
  cpu_window_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return emulator_->readMemory(address);
  });

  memory_viewer_window_ = std::make_unique<memory_viewer_window>();
  memory_viewer_window_->setOpen(true);

  // Set memory read callback for memory viewer
  // Use peek() instead of read() to avoid triggering soft switch side effects
  memory_viewer_window_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return emulator_->peekMemory(address);
  });

  // Set memory write callback for memory viewer
  memory_viewer_window_->setMemoryWriteCallback([this](uint16_t address, uint8_t value)
  {
    emulator_->writeMemory(address, value);
  });

  // Create video window
  video_window_ = std::make_unique<video_window>();
  video_window_->setOpen(true);

  // Set keyboard callback for video window
  video_window_->setKeyPressCallback([this](uint8_t key_code)
  {
    emulator_->keyDown(key_code);
  });

  // Initialize video texture with Metal device
  if (window_renderer_->getMetalDevice())
  {
    emulator_->initializeVideoTexture(window_renderer_->getMetalDevice());
  }

  // Load character ROM
  emulator_->loadCharacterROM("resources/roms/character/341-0160-A.bin");

  // Connect video_display to video_window
  video_window_->setVideoDisplay(emulator_->getVideoDisplay());

  // Create soft switches window
  soft_switches_window_ = std::make_unique<soft_switches_window>();
  soft_switches_window_->setOpen(false);  // Start closed by default

  // Set state callback (read-only, does not affect emulator state)
  // Use getSoftSwitchSnapshot() to include diagnostic values like CSW/KSW
  soft_switches_window_->setStateCallback([this]() -> Apple2e::SoftSwitchState
  {
    return emulator_->getSoftSwitchSnapshot();
  });

  // Load saved window visibility state
  loadWindowState();
}

void application::renderUI()
{
  renderMenuBar();

  // Create dockspace if docking is enabled
  ImGuiIO &io = window_renderer_->getIO();
  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
  {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  }

  // Update CPU window with current CPU state
  updateCPUWindow();

  // Render all windows
  if (cpu_window_)
  {
    cpu_window_->render();
  }

  if (memory_viewer_window_)
  {
    memory_viewer_window_->render();
  }

  if (video_window_)
  {
    video_window_->render();
  }

  if (soft_switches_window_)
  {
    soft_switches_window_->render();
  }
}

void application::renderMenuBar()
{
  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Warm Reset", "Ctrl+Reset"))
      {
        emulator_->warmReset();
      }
      if (ImGui::MenuItem("Cold Reset"))
      {
        emulator_->reset();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit"))
      {
        should_close_ = true;
        if (window_renderer_)
        {
          window_renderer_->close();
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
      // Toggle window visibility
      if (cpu_window_)
      {
        bool is_open = cpu_window_->isOpen();
        if (ImGui::MenuItem("CPU Registers", nullptr, &is_open))
        {
          cpu_window_->setOpen(is_open);
        }
      }

      if (memory_viewer_window_)
      {
        bool is_open = memory_viewer_window_->isOpen();
        if (ImGui::MenuItem("Memory Viewer", nullptr, &is_open))
        {
          memory_viewer_window_->setOpen(is_open);
        }
      }

      if (video_window_)
      {
        bool is_open = video_window_->isOpen();
        if (ImGui::MenuItem("Video Display", nullptr, &is_open))
        {
          video_window_->setOpen(is_open);
        }
      }

      if (soft_switches_window_)
      {
        bool is_open = soft_switches_window_->isOpen();
        if (ImGui::MenuItem("Soft Switches", nullptr, &is_open))
        {
          soft_switches_window_->setOpen(is_open);
        }
      }

      ImGui::Separator();

      // Video filtering mode toggle
      bool linear_filtering = ImGui_ImplMetal_GetSamplerLinear();
      if (ImGui::MenuItem("Linear Filtering", nullptr, &linear_filtering))
      {
        ImGui_ImplMetal_SetSamplerLinear(linear_filtering);
      }

      // Color fringing toggle
      auto* display = emulator_->getVideoDisplay();
      if (display)
      {
        bool color_fringing = display->isColorFringingEnabled();
        if (ImGui::MenuItem("Color Fringing", nullptr, &color_fringing))
        {
          display->setColorFringing(color_fringing);
        }
      }

      ImGui::Separator();

      // Alternate character set toggle (simulates front panel switch on real IIe)
      {
        bool altchar = emulator_->getSoftSwitchState().altchar_mode;
        if (ImGui::MenuItem("Alternate Charset", nullptr, &altchar))
        {
          emulator_->getMutableSoftSwitchState().altchar_mode = altchar;
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Audio"))
    {
      if (emulator_->isSpeakerInitialized())
      {
        // Mute toggle
        bool muted = emulator_->isSpeakerMuted();
        if (ImGui::MenuItem("Mute", nullptr, &muted))
        {
          emulator_->setSpeakerMuted(muted);
        }

        ImGui::Separator();

        // Volume slider
        float volume = emulator_->getSpeakerVolume();
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f"))
        {
          emulator_->setSpeakerVolume(volume);
        }

        ImGui::Separator();

        // Audio status
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "Audio: Active");
      }
      else
      {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Audio: Disabled");
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
      if (ImGui::MenuItem("About"))
      {
        // TODO: Show about dialog
      }
      ImGui::EndMenu();
    }

    // Warm Reset button directly in menu bar
    if (ImGui::Button("Reset"))
    {
      emulator_->warmReset();
    }

    ImGui::EndMainMenuBar();
  }
}

void application::updateCPUWindow()
{
  if (cpu_window_ && emulator_)
  {
    auto emu_state = emulator_->getCPUState();

    cpu_window::cpu_state state;
    state.pc = emu_state.pc;
    state.sp = emu_state.sp;
    state.p = emu_state.p;
    state.a = emu_state.a;
    state.x = emu_state.x;
    state.y = emu_state.y;
    state.total_cycles = emu_state.total_cycles;
    state.initialized = emu_state.initialized;

    cpu_window_->setCPUState(state);
  }
}

void application::update(float deltaTime)
{
  // Pause emulation completely when window loses focus
  bool has_focus = window_renderer_ && window_renderer_->hasFocus();

  // Track focus changes to reset speaker timing
  if (has_focus != had_focus_)
  {
    had_focus_ = has_focus;
    if (has_focus)
    {
      // Reset speaker timing when regaining focus to avoid audio glitches
      emulator_->resetSpeakerTiming();
    }
  }

  if (!has_focus)
  {
    return;
  }

  // Delegate to emulator
  emulator_->update(deltaTime);
}

void application::loadWindowState()
{
  if (!preferences_)
  {
    return;
  }

  // Load main window geometry
  if (window_renderer_)
  {
    // Check if we have saved window state
    if (preferences_->hasKey("window.main.x"))
    {
      int x = preferences_->getInt("window.main.x", 100);
      int y = preferences_->getInt("window.main.y", 100);
      int width = preferences_->getInt("window.main.width", 1280);
      int height = preferences_->getInt("window.main.height", 800);
      
      // Validate dimensions are reasonable
      if (width >= 640 && height >= 480)
      {
        window_renderer_->setWindowGeometry(x, y, width, height);
      }
    }

    // Restore maximized state after setting geometry
    if (preferences_->getBool("window.main.maximized", false))
    {
      window_renderer_->setMaximized(true);
    }
  }

  // Load window visibility states (with defaults)
  if (cpu_window_)
  {
    cpu_window_->setOpen(preferences_->getBool("window.cpu.visible", true));
  }

  if (memory_viewer_window_)
  {
    memory_viewer_window_->setOpen(preferences_->getBool("window.memory_viewer.visible", true));
  }

  if (video_window_)
  {
    video_window_->setOpen(preferences_->getBool("window.video.visible", true));
  }

  if (soft_switches_window_)
  {
    soft_switches_window_->setOpen(preferences_->getBool("window.soft_switches.visible", false));
  }
}

void application::saveWindowState()
{
  if (!preferences_)
  {
    return;
  }

  // Save main window geometry
  if (window_renderer_)
  {
    // Save maximized state
    bool maximized = window_renderer_->isMaximized();
    preferences_->setBool("window.main.maximized", maximized);

    // Only save position/size if not maximized (so we restore to normal size)
    if (!maximized)
    {
      auto [x, y] = window_renderer_->getWindowPosition();
      auto [width, height] = window_renderer_->getWindowSize();
      
      preferences_->setInt("window.main.x", x);
      preferences_->setInt("window.main.y", y);
      preferences_->setInt("window.main.width", width);
      preferences_->setInt("window.main.height", height);
    }
  }

  // Save window visibility states
  if (cpu_window_)
  {
    preferences_->setBool("window.cpu.visible", cpu_window_->isOpen());
  }

  if (memory_viewer_window_)
  {
    preferences_->setBool("window.memory_viewer.visible", memory_viewer_window_->isOpen());
  }

  if (video_window_)
  {
    preferences_->setBool("window.video.visible", video_window_->isOpen());
  }

  if (soft_switches_window_)
  {
    preferences_->setBool("window.soft_switches.visible", soft_switches_window_->isOpen());
  }

  // Save preferences to disk
  preferences_->save();
}
