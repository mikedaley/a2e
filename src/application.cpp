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
  // Configure IMGUI settings if needed
  // Additional IMGUI configuration can be done here
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

  renderCPUWindow();
  renderStatusWindow();
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
      ImGui::MenuItem("CPU Registers", nullptr, nullptr);
      ImGui::MenuItem("Memory", nullptr, nullptr);
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

void application::renderCPUWindow()
{
  // Set initial position only on first use to prevent jumping during resize
  ImGui::SetNextWindowPos(ImVec2(20, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
  
  if (ImGui::Begin("CPU Registers"))
  {
    if (cpu_)
    {
      ImGui::Text("Program Counter: 0x%04X", cpu_->getPC());
      ImGui::Text("Stack Pointer:   0x%02X", static_cast<int>(cpu_->getSP()));
      ImGui::Text("Status Register: 0x%02X", static_cast<int>(cpu_->getP()));
      ImGui::Text("Accumulator:     0x%02X", static_cast<int>(cpu_->getA()));
      ImGui::Text("X Register:      0x%02X", static_cast<int>(cpu_->getX()));
      ImGui::Text("Y Register:      0x%02X", static_cast<int>(cpu_->getY()));
    }
    else
    {
      ImGui::Text("CPU not initialized");
    }
  }
  ImGui::End();
}

void application::renderStatusWindow()
{
  // Set initial position only on first use to prevent jumping during resize
  ImGui::SetNextWindowPos(ImVec2(340, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
  
  if (ImGui::Begin("Status"))
  {
    ImGui::Text("Apple 2e Emulator v0.1.0");
    ImGui::Text("65C02 CPU initialized");
    ImGui::Separator();

    ImGuiIO &io = window_renderer_->getIO();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / io.Framerate,
                io.Framerate);
  }
  ImGui::End();
}

void application::update(float deltaTime)
{
  // Update emulator state here
  // For now, this is a placeholder
  (void)deltaTime; // Suppress unused parameter warning
}
