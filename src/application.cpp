#include "application.hpp"
#include <imgui.h>
#include <imgui_impl_metal_custom.h>
#include <iostream>
#include <iomanip>
#include <functional>
#include <chrono>

// CPU wrapper to hide template complexity
class application::cpu_wrapper
{
public:
  using ReadCallback = std::function<uint8_t(uint16_t)>;
  using WriteCallback = std::function<void(uint16_t, uint8_t)>;
  using CPU = MOS6502::CPU6502<ReadCallback, WriteCallback, MOS6502::CPUVariant::CMOS_65C02>;

  cpu_wrapper(ReadCallback read, WriteCallback write)
      : cpu_(std::move(read), std::move(write))
  {
  }

  void reset() { cpu_.reset(); }
  uint32_t executeInstruction() { return cpu_.executeInstruction(); }
  uint64_t getTotalCycles() const { return cpu_.getTotalCycles(); }
  uint16_t getPC() const { return cpu_.getPC(); }
  uint8_t getSP() const { return cpu_.getSP(); }
  uint8_t getP() const { return cpu_.getP(); }
  uint8_t getA() const { return cpu_.getA(); }
  uint8_t getX() const { return cpu_.getX(); }
  uint8_t getY() const { return cpu_.getY(); }
  void setPC(uint16_t val) { cpu_.setPC(val); }

private:
  CPU cpu_;
};

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

  // Shutdown speaker before other components to prevent audio issues
  if (speaker_)
  {
    speaker_->shutdown();
  }
}

bool application::initialize()
{
  try
  {
    std::cout << "Initializing Apple IIe Emulator..." << std::endl;

    // Create RAM (64KB with main/aux banks)
    ram_ = std::make_unique<RAM>();
    std::cout << "RAM initialized (64KB main + 64KB aux)" << std::endl;

    // Create ROM (12KB)
    rom_ = std::make_unique<ROM>();

    // Load Apple IIe ROMs from resources/roms folder
    if (!rom_->loadAppleIIeROMs())
    {
      std::cerr << "Error: Failed to load Apple IIe ROM files" << std::endl;
      std::cerr << "Please ensure ROM files are present in resources/roms/" << std::endl;
      return false;
    }

    // Create keyboard
    keyboard_ = std::make_unique<Keyboard>();
    std::cout << "Keyboard initialized" << std::endl;

    // Create speaker
    speaker_ = std::make_unique<Speaker>();
    if (!speaker_->initialize())
    {
      std::cerr << "Warning: Failed to initialize speaker (audio disabled)" << std::endl;
    }
    else
    {
      std::cout << "Speaker initialized" << std::endl;
    }

    // Create video display (generates video output texture)
    video_display_ = std::make_unique<video_display>();
    std::cout << "Video display initialized" << std::endl;

    // Create MMU (handles memory mapping and soft switches)
    mmu_ = std::make_unique<MMU>(*ram_, *rom_, keyboard_.get(), speaker_.get());
    std::cout << "MMU initialized" << std::endl;

    // Create bus
    bus_ = std::make_unique<Bus>();
    std::cout << "Bus initialized" << std::endl;

    // Define memory read callback (routes through MMU)
    auto read = [this](uint16_t address) -> uint8_t
    {
      return mmu_->read(address);
    };

    // Define memory write callback (routes through MMU)
    auto write = [this](uint16_t address, uint8_t value) -> void
    {
      mmu_->write(address, value);
    };

    // Create CPU with 65C02 variant
    cpu_ = std::make_unique<cpu_wrapper>(read, write);
    std::cout << "CPU initialized (65C02)" << std::endl;

    // Reset CPU
    cpu_->reset();
    std::cout << "CPU reset complete" << std::endl;
    std::cout << "Initial PC: $" << std::hex << std::uppercase << cpu_->getPC() << std::dec << std::endl;

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

    std::cout << "\nEmulator initialization complete!" << std::endl;
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
    return mmu_->read(address);
  });

  memory_viewer_window_ = std::make_unique<memory_viewer_window>();
  memory_viewer_window_->setOpen(true);

  // Set memory read callback for memory viewer
  // Use peek() instead of read() to avoid triggering soft switch side effects
  memory_viewer_window_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return mmu_->peek(address);
  });

  // Set memory write callback for memory viewer
  memory_viewer_window_->setMemoryWriteCallback([this](uint16_t address, uint8_t value)
  {
    mmu_->write(address, value);
  });

  // Create video window
  video_window_ = std::make_unique<video_window>();
  video_window_->setOpen(true);

  // Set keyboard callback for video window
  video_window_->setKeyPressCallback([this](uint8_t key_code)
  {
    if (keyboard_)
    {
      keyboard_->keyDown(key_code);
    }
  });

  // Configure video_display with memory callbacks
  // Video reads directly from RAM, bypassing MMU soft switches
  // This matches real hardware where video circuitry reads display memory directly
  video_display_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return ram_->getMainBank()[address];
  });

  // Set auxiliary memory read callback for 80-column mode
  video_display_->setAuxMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return ram_->getAuxBank()[address];
  });

  // Set video mode callback for video_display
  video_display_->setVideoModeCallback([this]() -> Apple2e::SoftSwitchState
  {
    if (mmu_)
    {
      return mmu_->getSoftSwitchState();
    }
    return Apple2e::SoftSwitchState();
  });

  // Initialize video texture with Metal device
  if (window_renderer_->getMetalDevice())
  {
    video_display_->initializeTexture(window_renderer_->getMetalDevice());
  }

  // Load character ROM
  video_display_->loadCharacterROM("resources/roms/character/341-0160-A.bin");

  // Connect video_display to video_window
  video_window_->setVideoDisplay(video_display_.get());

  // Create soft switches window
  soft_switches_window_ = std::make_unique<soft_switches_window>();
  soft_switches_window_->setOpen(false);  // Start closed by default

  // Set state callback (read-only, does not affect emulator state)
  // Use getSoftSwitchSnapshot() to include diagnostic values like CSW/KSW
  soft_switches_window_->setStateCallback([this]() -> Apple2e::SoftSwitchState
  {
    if (mmu_)
    {
      return mmu_->getSoftSwitchSnapshot();
    }
    return Apple2e::SoftSwitchState();
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
        warmReset();
      }
      if (ImGui::MenuItem("Cold Reset"))
      {
        reset();
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
      if (video_display_)
      {
        bool color_fringing = video_display_->isColorFringingEnabled();
        if (ImGui::MenuItem("Color Fringing", nullptr, &color_fringing))
        {
          video_display_->setColorFringing(color_fringing);
        }
      }

      ImGui::Separator();

      // Alternate character set toggle (simulates front panel switch on real IIe)
      if (mmu_)
      {
        bool altchar = mmu_->getSoftSwitchState().altchar_mode;
        if (ImGui::MenuItem("Alternate Charset", nullptr, &altchar))
        {
          mmu_->getSoftSwitchState().altchar_mode = altchar;
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Audio"))
    {
      if (speaker_)
      {
        // Mute toggle
        bool muted = speaker_->isMuted();
        if (ImGui::MenuItem("Mute", nullptr, &muted))
        {
          speaker_->setMuted(muted);
        }

        ImGui::Separator();

        // Volume slider
        float volume = speaker_->getVolume();
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f"))
        {
          speaker_->setVolume(volume);
        }

        ImGui::Separator();

        // Audio status
        if (speaker_->isInitialized())
        {
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "Audio: Active");
        }
        else
        {
          ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Audio: Disabled");
        }
      }
      else
      {
        ImGui::TextDisabled("Speaker not available");
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
      warmReset();
    }

    ImGui::EndMainMenuBar();
  }
}

void application::updateCPUWindow()
{
  if (cpu_window_ && cpu_)
  {
    cpu_window::cpu_state state;
    state.pc = cpu_->getPC();
    state.sp = cpu_->getSP();
    state.p = cpu_->getP();
    state.a = cpu_->getA();
    state.x = cpu_->getX();
    state.y = cpu_->getY();
    state.total_cycles = cpu_->getTotalCycles();
    state.initialized = true;

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
    if (has_focus && speaker_)
    {
      // Reset speaker timing when regaining focus to avoid audio glitches
      speaker_->reset();
    }
  }

  if (!has_focus)
  {
    return;
  }

  // On first update with focus, reset speaker to sync with current CPU cycle
  // This prevents a large skip due to cycles elapsed during initialization
  if (first_update_ && speaker_)
  {
    first_update_ = false;
    speaker_->reset();
  }

  // Execute CPU instructions based on actual elapsed time
  // Apple IIe runs at approximately 1.023 MHz (1,023,000 cycles per second)
  constexpr double CPU_CLOCK_HZ = 1023000.0;

  if (cpu_ && deltaTime > 0.0f)
  {
    // Calculate how many cycles should have elapsed based on real time
    // Cap deltaTime to prevent spiral of death if app hangs
    float capped_delta = deltaTime > 0.1f ? 0.1f : deltaTime;

    uint64_t cycles_to_execute = static_cast<uint64_t>(capped_delta * CPU_CLOCK_HZ);
    uint64_t target_cycles = cpu_->getTotalCycles() + cycles_to_execute;

    // Execute instructions until we've consumed the cycles for this frame
    while (cpu_->getTotalCycles() < target_cycles)
    {
      // Update MMU cycle count for speaker timing
      if (mmu_)
      {
        mmu_->setCycleCount(cpu_->getTotalCycles());
      }
      cpu_->executeInstruction();
    }

    // Update speaker audio output
    if (speaker_)
    {
      speaker_->update(cpu_->getTotalCycles());
    }
  }
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

void application::reset()
{
  // Hard reset - simulate power cycle (cold boot)

  // Clear all RAM (both main and aux banks)
  if (ram_)
  {
    ram_->getMainBank().fill(0x00);
    ram_->getAuxBank().fill(0x00);
  }

  // Reset soft switches to power-on state
  if (mmu_)
  {
    mmu_->getSoftSwitchState() = Apple2e::SoftSwitchState();
  }

  // Clear keyboard strobe
  if (keyboard_)
  {
    keyboard_->write(Apple2e::KBDSTRB, 0);
  }

  // Reset CPU (reads reset vector from ROM)
  if (cpu_)
  {
    cpu_->reset();
  }
}

void application::warmReset()
{
  // Warm reset - jump directly to BASIC prompt
  // This bypasses the boot ROM's peripheral card scanning

  // Reset soft switches to power-on state (text mode, etc.)
  if (mmu_)
  {
    mmu_->getSoftSwitchState() = Apple2e::SoftSwitchState();
  }

  // Clear keyboard strobe
  if (keyboard_)
  {
    keyboard_->write(Apple2e::KBDSTRB, 0);
  }

  // Jump directly to Applesoft BASIC cold start
  // $E003 is the BASIC warm start entry point
  // This displays the ] prompt and enters the BASIC interpreter
  if (cpu_)
  {
    cpu_->setPC(0xE003);
    std::cout << "Warm reset: jumping to BASIC at $E003" << std::endl;
  }
}
