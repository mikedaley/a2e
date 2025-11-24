#include "application.hpp"
#include <imgui.h>
#include <iostream>
#include <iomanip>
#include <functional>

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
  uint16_t getPC() const { return cpu_.getPC(); }
  uint8_t getSP() const { return cpu_.getSP(); }
  uint8_t getP() const { return cpu_.getP(); }
  uint8_t getA() const { return cpu_.getA(); }
  uint8_t getX() const { return cpu_.getX(); }
  uint8_t getY() const { return cpu_.getY(); }

private:
  CPU cpu_;
};

application::application()
    : preferences_(std::make_unique<preferences>("a2e"))
{
}

application::~application()
{
  // Save preferences on exit
  if (preferences_)
  {
    saveWindowStates();
    preferences_->save();
  }
}

bool application::initialize()
{
  try
  {
    // Load preferences
    if (preferences_)
    {
      preferences_->load();
    }

    // Create bus
    bus_ = std::make_unique<Bus>();

    // Create RAM (64KB with main/aux banks)
    ram_ = std::make_unique<RAM>();

    // Create ROM (16KB)
    rom_ = std::make_unique<ROM>();
    // TODO: Load ROM from file
    // rom_->loadFromFile("path/to/rom.bin");

    // Create MMU (handles memory mapping and soft switches)
    // Pass keyboard reference so MMU can route keyboard I/O
    mmu_ = std::make_unique<MMU>(*ram_, *rom_, keyboard_.get());

    // Create keyboard
    keyboard_ = std::make_unique<Keyboard>();

    // Create video
    video_ = std::make_unique<Video>(*ram_);
    if (!video_->initialize())
    {
      std::cerr << "Failed to initialize video" << std::endl;
      return false;
    }

    // Register devices with bus (order matters - MMU should be last as it handles entire address space)
    // Register Keyboard first (for I/O addresses $C000-$C010)
    // Note: We create a new Keyboard instance for the bus, but MMU also has a reference to the original
    // In a real system, Keyboard would be accessed through MMU, but for flexibility we register it separately too
    bus_->registerDevice(std::make_unique<Keyboard>());
    // Register MMU last (it handles the entire address space and routes internally)
    bus_->registerDevice(std::make_unique<MMU>(*ram_, *rom_, keyboard_.get()));

    // Define memory read callback (routes through bus)
    auto read = [this](uint16_t address) -> uint8_t
    {
      return bus_->read(address);
    };

    // Define memory write callback (routes through bus)
    auto write = [this](uint16_t address, uint8_t value) -> void
    {
      bus_->write(address, value);
    };

    // Create CPU with 65C02 variant
    cpu_ = std::make_unique<cpu_wrapper>(read, write);

    // Reset CPU
    cpu_->reset();

    // Configure window renderer
    window_renderer::config config;
    config.title = "Apple 2e Emulator";
    config.width = 1280;
    config.height = 800;
    config.vsync = true;
    config.docking = true;    // Enable docking for better UI organization
    config.viewports = false; // Disable multi-viewport for now

    // Create window renderer
    window_renderer_ = std::make_unique<window_renderer>(config);

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
  status_window_ = std::make_unique<status_window>(&window_renderer_->getIO());

  // Load window visibility states from preferences
  loadWindowStates();
}

void application::renderUI()
{
  renderMenuBar();

  // Create dockspace if docking is enabled
  ImGuiIO &io = window_renderer_->getIO();
  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
  {
    // Use the simpler DockSpaceOverViewport API
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  }

  // Update CPU window with current CPU state
  updateCPUWindow();

  // Render all windows
  if (cpu_window_)
  {
    cpu_window_->render();
  }

  if (status_window_)
  {
    status_window_->render();
  }

  // Render video output window
  if (video_ && video_->getSurface())
  {
    ImGui::Begin("Apple IIe Display");
    auto dims = video_->getDimensions();
    // Convert SDL_Surface to ImTextureID for ImGui display
    // For now, we'll use a placeholder - proper texture integration will be added later
    ImGui::Text("Video: %dx%d", dims.first, dims.second);
    ImGui::End();
  }
}

void application::renderMenuBar()
{
  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
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
          saveWindowStates(); // Save immediately when changed
        }
      }

      if (status_window_)
      {
        bool is_open = status_window_->isOpen();
        if (ImGui::MenuItem("Status", nullptr, &is_open))
        {
          status_window_->setOpen(is_open);
          saveWindowStates(); // Save immediately when changed
        }
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
    state.initialized = true;

    cpu_window_->setCPUState(state);
  }
}

void application::update(float deltaTime)
{
  // Update emulator state
  (void)deltaTime; // Suppress unused parameter warning

  // Update video soft switches from MMU
  if (mmu_ && video_)
  {
    video_->updateSoftSwitches(mmu_->getSoftSwitchState());
  }

  // Render video frame
  if (video_)
  {
    video_->render();
  }

  // TODO: Execute CPU cycles
  // For now, we'll add CPU execution in a future update
}

void application::loadWindowStates()
{
  if (!preferences_)
  {
    return;
  }

  // Load window visibility states (default to true if not found)
  if (cpu_window_)
  {
    bool is_open = preferences_->getBool("window.cpu_registers.visible", true);
    cpu_window_->setOpen(is_open);
  }

  if (status_window_)
  {
    bool is_open = preferences_->getBool("window.status.visible", true);
    status_window_->setOpen(is_open);
  }
}

void application::saveWindowStates()
{
  if (!preferences_)
  {
    return;
  }

  // Save window visibility states
  if (cpu_window_)
  {
    preferences_->setBool("window.cpu_registers.visible", cpu_window_->isOpen());
  }

  if (status_window_)
  {
    preferences_->setBool("window.status.visible", status_window_->isOpen());
  }

  // Save to disk immediately
  preferences_->save();
}
