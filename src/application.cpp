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
    : memory_(std::make_unique<memory>())
{
}

application::~application() = default;

bool application::initialize()
{
  try
  {
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

    // Define memory read callback
    auto read = [this](uint16_t address) -> uint8_t
    {
      return memory_->read(address);
    };

    // Define memory write callback
    auto write = [this](uint16_t address, uint8_t value) -> void
    {
      memory_->write(address, value);
    };

    // Create CPU with 65C02 variant
    cpu_ = std::make_unique<cpu_wrapper>(read, write);

    // Reset CPU
    cpu_->reset();

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

      if (status_window_)
      {
        bool is_open = status_window_->isOpen();
        if (ImGui::MenuItem("Status", nullptr, &is_open))
        {
          status_window_->setOpen(is_open);
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
  // Update emulator state here
  // For now, this is a placeholder
  (void)deltaTime; // Suppress unused parameter warning
}
