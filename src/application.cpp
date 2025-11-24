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
    // Configure window manager
    window_manager::config config;
    config.title = "Apple 2e Emulator";
    config.width = 1280;
    config.height = 800;
    config.vsync = true;
    config.docking = true;    // Enable docking for better UI organization
    config.viewports = false; // Disable multi-viewport for now

    // Create window manager
    window_manager_ = std::make_unique<window_manager>(config);

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
  if (!window_manager_)
  {
    std::cerr << "Application not initialized" << std::endl;
    return 1;
  }

  // Setup UI
  setupUI();

  // Run the main loop
  return window_manager_->run(
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

// Docking support is only available in the IMGUI docking branch
// If not available, windows will just float normally
#ifdef IMGUI_HAS_DOCK
  ImGuiIO &io = window_manager_->getIO();
  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
  {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (viewport)
    {
      // Create a simple dockspace window
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);
      ImGui::SetNextWindowViewport(viewport->ID);

      ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
      window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
      window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
      window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

      if (ImGui::Begin("DockSpace", nullptr, window_flags))
      {
        ImGui::PopStyleVar(3);
        // Dockspace would go here if available
        ImGui::End();
      }
      else
      {
        ImGui::PopStyleVar(3);
      }
    }
  }
#endif

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
        if (window_manager_)
        {
          window_manager_->close();
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
  
  // Lock window position/size during live resize to prevent jitter
  ImGuiWindowFlags flags = 0;
  if (window_manager_ && window_manager_->isInLiveResize())
  {
    flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  }
  
  if (ImGui::Begin("CPU Registers", nullptr, flags))
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
  
  // Lock window position/size during live resize to prevent jitter
  ImGuiWindowFlags flags = 0;
  if (window_manager_ && window_manager_->isInLiveResize())
  {
    flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  }
  
  if (ImGui::Begin("Status", nullptr, flags))
  {
    ImGui::Text("Apple 2e Emulator v0.1.0");
    ImGui::Text("65C02 CPU initialized");
    ImGui::Separator();

    ImGuiIO &io = window_manager_->getIO();
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
