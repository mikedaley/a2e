#include "application.hpp"
#include <imgui.h>
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

private:
  CPU cpu_;
};

application::application()
{
}

application::~application()
{
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

    // Load Apple IIe ROMs from include/roms folder
    if (!rom_->loadAppleIIeROMs())
    {
      std::cerr << "Error: Failed to load Apple IIe ROM files" << std::endl;
      std::cerr << "Please ensure ROM files are present in include/roms/" << std::endl;
      return false;
    }

    // Create keyboard
    keyboard_ = std::make_unique<Keyboard>();
    std::cout << "Keyboard initialized" << std::endl;

    // Create MMU (handles memory mapping and soft switches)
    mmu_ = std::make_unique<MMU>(*ram_, *rom_, keyboard_.get());
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

  memory_viewer_window_ = std::make_unique<memory_viewer_window>();
  memory_viewer_window_->setOpen(true);

  // Set memory read callback for memory viewer
  memory_viewer_window_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return mmu_->read(address);
  });

  // Create text screen window
  text_screen_window_ = std::make_unique<text_screen_window>();
  text_screen_window_->setOpen(true);

  // Set memory read callback for text screen
  text_screen_window_->setMemoryReadCallback([this](uint16_t address) -> uint8_t
  {
    return mmu_->read(address);
  });
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

  if (text_screen_window_)
  {
    text_screen_window_->render();
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

      if (text_screen_window_)
      {
        bool is_open = text_screen_window_->isOpen();
        if (ImGui::MenuItem("Text Screen", nullptr, &is_open))
        {
          text_screen_window_->setOpen(is_open);
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Navigate"))
    {
      if (memory_viewer_window_)
      {
        if (ImGui::MenuItem("Zero Page ($0000)"))
        {
          memory_viewer_window_->setBaseAddress(0x0000);
        }
        if (ImGui::MenuItem("Stack ($0100)"))
        {
          memory_viewer_window_->setBaseAddress(0x0100);
        }
        if (ImGui::MenuItem("Text Page 1 ($0400)"))
        {
          memory_viewer_window_->setBaseAddress(0x0400);
        }
        if (ImGui::MenuItem("Text Page 2 ($0800)"))
        {
          memory_viewer_window_->setBaseAddress(0x0800);
        }
        if (ImGui::MenuItem("Hi-Res Page 1 ($2000)"))
        {
          memory_viewer_window_->setBaseAddress(0x2000);
        }
        if (ImGui::MenuItem("Hi-Res Page 2 ($4000)"))
        {
          memory_viewer_window_->setBaseAddress(0x4000);
        }
        if (ImGui::MenuItem("I/O ($C000)"))
        {
          memory_viewer_window_->setBaseAddress(0xC000);
        }
        if (ImGui::MenuItem("ROM ($D000)"))
        {
          memory_viewer_window_->setBaseAddress(0xD000);
        }
        if (ImGui::MenuItem("Reset Vector ($FFFC)"))
        {
          memory_viewer_window_->setBaseAddress(0xFFF0);
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
  // Execute CPU instructions
  // Apple IIe runs at approximately 1.023 MHz (1,023,000 cycles per second)
  constexpr uint64_t CYCLES_PER_SECOND = 1023000;

  // Calculate how many cycles to execute this frame based on deltaTime
  if (cpu_ && deltaTime > 0.0f)
  {
    uint64_t cycles_to_execute = static_cast<uint64_t>(CYCLES_PER_SECOND * deltaTime);
    uint64_t start_cycles = cpu_->getTotalCycles();
    uint64_t target_cycles = start_cycles + cycles_to_execute;

    // Execute instructions until we've consumed enough cycles
    while (cpu_->getTotalCycles() < target_cycles)
    {
      cpu_->executeInstruction();
    }
  }
}
