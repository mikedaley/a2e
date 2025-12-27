#include "application.hpp"
#include "utils/paste_handler.hpp"
#include "utils/resource_path.hpp"
#include <imgui.h>
#include <imgui_impl_metal_custom.h>
#include <SDL3/SDL_clipboard.h>
#include <iostream>
#include <filesystem>

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

    // Load character ROM
    emulator_->loadCharacterROM("resources/roms/character/341-0160-A.bin");

    // Auto-load test disk image if available
    auto* diskController = emulator_->getDiskController();
    if (diskController)
    {
      const char* testDisk = "disk_images/Apple DOS 3.3 January 1983.dsk";
      if (std::filesystem::exists(testDisk))
      {
        if (diskController->insertDisk(0, testDisk))
        {
          std::cout << "Auto-loaded test disk: " << testDisk << std::endl;
        }
      }
    }

    // Configure window renderer
    window_renderer::config config;
    config.title = "Apple IIe Emulator";
    config.width = 1280;
    config.height = 800;
    config.vsync = false;  // Disable VSync to allow precise 60 FPS timing
    config.docking = true;
    config.viewports = false;

    // Create window renderer
    window_renderer_ = std::make_unique<window_renderer>(config);

    // Create window manager
    window_manager_ = std::make_unique<window_manager>();

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
  // Initialize window manager with emulator and Metal device
  window_manager_->initialize(*emulator_, window_renderer_->getMetalDevice());

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

  // Render all windows
  window_manager_->render();

  // Render modal dialogs
  renderDialogs();
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
      if (ImGui::MenuItem("Save State"))
      {
        emulator_->saveState(getSaveStatePath());
      }
      if (ImGui::MenuItem("Load State"))
      {
        if (emulator::savedStateExists(getSaveStatePath()))
        {
          emulator_->loadState(getSaveStatePath());
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit"))
      {
        requestClose();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
      if (ImGui::MenuItem("Paste", "Cmd+V"))
      {
        // Get clipboard text and paste it
        char* clipboard = SDL_GetClipboardText();
        if (clipboard)
        {
          PasteHandler::paste(clipboard);
          SDL_free(clipboard);
        }
      }
      if (PasteHandler::isPasting())
      {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Pasting...");
        if (ImGui::MenuItem("Cancel Paste"))
        {
          PasteHandler::clear();
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
      // Toggle window visibility
      if (auto* win = window_manager_->getCPUWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("CPU Registers", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getMemoryViewerWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Memory Viewer", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getVideoWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Video Display", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getSoftSwitchesWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Soft Switches", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getDebuggerWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Debugger", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getMemoryAccessWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Memory Access", nullptr, &is_open))
        {
          win->setOpen(is_open);
        }
      }

      if (auto* win = window_manager_->getDiskWindow())
      {
        bool is_open = win->isOpen();
        if (ImGui::MenuItem("Disk II Controller", nullptr, &is_open))
        {
          win->setOpen(is_open);
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
    if (ImGui::Button("Wam Reset"))
    {
      emulator_->warmReset();
    }

    if (ImGui::Button("Cold Reset"))
    {
      emulator_->reset();
    }

    ImGui::EndMainMenuBar();
  }
}

void application::update(float deltaTime)
{
  // Track focus changes to reset speaker timing
  bool has_focus = window_renderer_ && window_renderer_->hasFocus();
  if (has_focus != had_focus_)
  {
    had_focus_ = has_focus;
    if (has_focus)
    {
      // Reset speaker timing when regaining focus to avoid audio glitches
      emulator_->resetSpeakerTiming();
    }
  }

  // Handle Cmd+V keyboard shortcut for paste
  ImGuiIO& io = ImGui::GetIO();
  if (io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_V))
  {
    char* clipboard = SDL_GetClipboardText();
    if (clipboard)
    {
      PasteHandler::paste(clipboard);
      SDL_free(clipboard);
    }
  }

  // Update paste handler - feeds characters to keyboard
  PasteHandler::update(*emulator_);

  // Update emulator using audio-driven timing
  emulator_->update();

  // Update all windows
  window_manager_->update(deltaTime);
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

  // Load window visibility states
  if (window_manager_)
  {
    window_manager_->loadState(*preferences_);
  }

  // Load breakpoints
  if (emulator_ && emulator_->getBreakpointManager())
  {
    std::string bp_data = preferences_->getString("debugger.breakpoints", "");
    if (!bp_data.empty())
    {
      emulator_->getBreakpointManager()->deserialize(bp_data);
    }
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
  if (window_manager_)
  {
    window_manager_->saveState(*preferences_);
  }

  // Save breakpoints
  if (emulator_ && emulator_->getBreakpointManager())
  {
    std::string bp_data = emulator_->getBreakpointManager()->serialize();
    preferences_->setString("debugger.breakpoints", bp_data);
  }

  // Save preferences to disk
  preferences_->save();
}

void application::renderDialogs()
{
  // Save state dialog (shown on exit)
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  if (show_save_state_dialog_)
  {
    ImGui::OpenPopup("Save State Before Exit?");
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("Save State Before Exit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Do you want to save the emulator state\nbefore exiting?");
    ImGui::Separator();

    if (ImGui::Button("Save", ImVec2(100, 0)))
    {
      emulator_->saveState(getSaveStatePath());
      show_save_state_dialog_ = false;
      ImGui::CloseCurrentPopup();

      // Actually close the app
      should_close_ = true;
      if (window_renderer_)
      {
        window_renderer_->close();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(100, 0)))
    {
      show_save_state_dialog_ = false;
      ImGui::CloseCurrentPopup();

      // Actually close the app
      should_close_ = true;
      if (window_renderer_)
      {
        window_renderer_->close();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)))
    {
      show_save_state_dialog_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

std::string application::getSaveStatePath() const
{
  // Get the application support directory for save files
  std::filesystem::path save_dir = getResourcePath();
  save_dir = save_dir.parent_path() / "SaveState";

  // Create directory if it doesn't exist
  std::filesystem::create_directories(save_dir);

  return (save_dir / "state.a2e").string();
}

void application::requestClose()
{
  // Show the save state dialog instead of closing immediately
  show_save_state_dialog_ = true;
}
