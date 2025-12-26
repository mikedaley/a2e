#include "emulator/emulator.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <chrono>

// CPU wrapper to hide template complexity
class emulator::cpu_wrapper
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

  // Setters for state restore
  void setPC(uint16_t val) { cpu_.setPC(val); }
  void setSP(uint8_t val) { cpu_.setSP(val); }
  void setP(uint8_t val) { cpu_.setP(val); }
  void setA(uint8_t val) { cpu_.setA(val); }
  void setX(uint8_t val) { cpu_.setX(val); }
  void setY(uint8_t val) { cpu_.setY(val); }

private:
  CPU cpu_;
};

emulator::emulator() = default;

emulator::~emulator()
{
  // Shutdown speaker before other components to prevent audio issues
  if (speaker_)
  {
    speaker_->shutdown();
  }
}

bool emulator::initialize()
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

    // Create Disk II controller (slot 6)
    disk_controller_ = std::make_unique<Disk2Controller>();
    if (!disk_controller_->initialize())
    {
      std::cerr << "Warning: Failed to initialize Disk II controller" << std::endl;
    }
    mmu_->setDiskController(disk_controller_.get());
    std::cout << "Disk II controller initialized (Slot 6)" << std::endl;

    // Create memory access tracker for visualization
    access_tracker_ = std::make_unique<memory_access_tracker>();
    mmu_->setAccessTracker(access_tracker_.get());
    std::cout << "Memory access tracker initialized" << std::endl;

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

    // Set up disk controller cycle callback now that CPU is available
    if (disk_controller_)
    {
      disk_controller_->setCycleCallback([this]() -> uint64_t
      {
        return cpu_ ? cpu_->getTotalCycles() : 0;
      });
    }

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

    // Create breakpoint manager for debugging
    breakpoint_mgr_ = std::make_unique<breakpoint_manager>();
    std::cout << "Breakpoint manager initialized" << std::endl;

    std::cout << "\nEmulator initialization complete!" << std::endl;
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Emulator initialization failed: " << e.what() << std::endl;
    return false;
  }
}

void emulator::update()
{
  // On first update, reset speaker to sync with current CPU cycle
  if (first_update_ && speaker_)
  {
    first_update_ = false;
    speaker_->reset();
  }

  if (!cpu_)
  {
    return;
  }

  // Audio-driven timing: run CPU cycles based on audio buffer fill level
  // This keeps emulation perfectly in sync with audio output

  // Apple IIe runs at approximately 1.023 MHz
  // At 44100 Hz sample rate, that's ~23.2 cycles per sample
  // One frame at 60fps = ~17050 cycles
  constexpr uint64_t CYCLES_PER_FRAME = 17050;

  // Check audio buffer fill level
  float bufferFill = 0.5f;  // Default to 50% if no speaker
  if (speaker_ && speaker_->isInitialized())
  {
    bufferFill = speaker_->getBufferFillPercentage();
  }

  // Target buffer fill is around 50%
  constexpr float TARGET_BUFFER_FILL = 0.5f;
  constexpr float MIN_BUFFER_FILL = 0.2f;
  constexpr float MAX_BUFFER_FILL = 0.8f;

  // If buffer is too full, don't run any cycles - let audio catch up
  if (bufferFill > MAX_BUFFER_FILL)
  {
    return;
  }

  // Calculate cycles to run based on buffer level
  uint64_t cyclesToRun;
  if (bufferFill < MIN_BUFFER_FILL)
  {
    // Buffer is low, run more cycles to catch up
    cyclesToRun = CYCLES_PER_FRAME * 2;
  }
  else if (bufferFill < TARGET_BUFFER_FILL)
  {
    // Buffer is below target, run normal amount
    cyclesToRun = CYCLES_PER_FRAME;
  }
  else
  {
    // Buffer is above target but not too full, run fewer cycles
    cyclesToRun = CYCLES_PER_FRAME / 2;
  }

  // Execute the calculated number of cycles
  uint64_t targetCycles = cpu_->getTotalCycles() + cyclesToRun;

  while (cpu_->getTotalCycles() < targetCycles)
  {
    // Check if execution is paused
    if (exec_state_ == execution_state::PAUSED)
    {
      break;
    }

    // Check execution breakpoints before instruction
    if (breakpoint_mgr_ && breakpoint_mgr_->checkExecution(cpu_->getPC()))
    {
      exec_state_ = execution_state::PAUSED;
      break;
    }

    // Track stack pointer for step-out detection
    uint8_t prev_sp = cpu_->getSP();

    // Update MMU cycle count BEFORE instruction for accurate disk timing
    // This ensures disk reads during instruction execution see correct cycles
    if (mmu_)
    {
      mmu_->setCycleCount(cpu_->getTotalCycles());
    }

    cpu_->executeInstruction();

    // Handle step modes after instruction execution
    if (exec_state_ == execution_state::STEP_OVER)
    {
      exec_state_ = execution_state::PAUSED;
      break;  // Stop immediately after one instruction
    }
    else if (exec_state_ == execution_state::STEP_OUT)
    {
      // Check if stack unwound (returned from subroutine)
      // SP increases when returning (RTS pops return address)
      if (cpu_->getSP() > prev_sp)
      {
        exec_state_ = execution_state::PAUSED;
        break;  // Stop immediately after returning
      }
    }
  }

  // Update speaker with current cycle count
  if (speaker_)
  {
    speaker_->update(cpu_->getTotalCycles());
  }
}

void emulator::reset()
{
  // Hard reset - simulate power cycle (cold boot)

  // Clear all RAM (both main and aux banks)
  if (ram_)
  {

    for (int i = 0; i < 65536; i++) {
        ram_->getMainBank()[i] = ((i >> 1) & 0x01) ? 0x00 : 0xFF;
    }
    // Or add some randomness
    for (int i = 0; i < 65536; i++) {
        uint8_t base = ((i >> 1) & 0x01) ? 0x00 : 0xFF;
        ram_->getMainBank()[i] = (rand() % 100 < 95) ? base : (rand() & 0xFF);
    }


    // ram_->getMainBank().fill(0x00);
    ram_->getAuxBank().fill(0x00);
  }

  // Reset soft switches to power-on state
  if (mmu_)
  {
    mmu_->getSoftSwitchState() = Apple2e::SoftSwitchState();
  }

  //Clear keyboard strobe
  if (keyboard_)
  {
    keyboard_->write(Apple2e::KBDSTRB, 0);
  }

  // Reset disk controller
  if (disk_controller_)
  {
    disk_controller_->reset();
  }

  // Reset CPU (reads reset vector from ROM)
  if (cpu_)
  {
    cpu_->reset();
  }

  // Reset first update flag to resync speaker
  first_update_ = true;
}

void emulator::warmReset()
{
  // Warm reset - preserves RAM but resets CPU
  // Simulates pressing the RESET button on real hardware

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

  // Trigger CPU reset - reads reset vector from $FFFC/$FFFD
  // On Apple IIe, this jumps to $FA62 (ROM initialization)
  // The ROM code will handle boot sequence and eventually get to BASIC if appropriate
  if (cpu_)
  {
    cpu_->reset();
    std::cout << "Warm reset: CPU reset triggered (vector at $FFFC/$FFFD)" << std::endl;
  }

  // Reset first update flag to resync speaker
  first_update_ = true;
}

emulator::cpu_state emulator::getCPUState() const
{
  cpu_state state;
  if (cpu_)
  {
    state.pc = cpu_->getPC();
    state.sp = cpu_->getSP();
    state.p = cpu_->getP();
    state.a = cpu_->getA();
    state.x = cpu_->getX();
    state.y = cpu_->getY();
    state.total_cycles = cpu_->getTotalCycles();
    state.initialized = true;
  }
  return state;
}

uint8_t emulator::readMemory(uint16_t address) const
{
  if (mmu_)
  {
    return mmu_->read(address);
  }
  return 0;
}

uint8_t emulator::peekMemory(uint16_t address) const
{
  if (mmu_)
  {
    return mmu_->peek(address);
  }
  return 0;
}

void emulator::writeMemory(uint16_t address, uint8_t value)
{
  if (mmu_)
  {
    mmu_->write(address, value);
  }
}

Apple2e::SoftSwitchState emulator::getSoftSwitchState() const
{
  if (mmu_)
  {
    return mmu_->getSoftSwitchState();
  }
  return Apple2e::SoftSwitchState();
}

Apple2e::SoftSwitchState emulator::getSoftSwitchSnapshot() const
{
  if (mmu_)
  {
    return mmu_->getSoftSwitchSnapshot();
  }
  return Apple2e::SoftSwitchState();
}

Apple2e::SoftSwitchState& emulator::getMutableSoftSwitchState()
{
  return mmu_->getSoftSwitchState();
}

void emulator::keyDown(uint8_t key_code)
{
  if (keyboard_)
  {
    keyboard_->keyDown(key_code);
  }
}

bool emulator::isKeyboardStrobeSet() const
{
  if (keyboard_)
  {
    return keyboard_->isStrobeSet();
  }
  return false;
}

bool emulator::isSpeakerInitialized() const
{
  return speaker_ && speaker_->isInitialized();
}

bool emulator::isSpeakerMuted() const
{
  return speaker_ ? speaker_->isMuted() : true;
}

void emulator::setSpeakerMuted(bool muted)
{
  if (speaker_)
  {
    speaker_->setMuted(muted);
  }
}

float emulator::getSpeakerVolume() const
{
  return speaker_ ? speaker_->getVolume() : 0.0f;
}

void emulator::setSpeakerVolume(float volume)
{
  if (speaker_)
  {
    speaker_->setVolume(volume);
  }
}

void emulator::resetSpeakerTiming()
{
  if (speaker_)
  {
    speaker_->reset();
  }
}

float emulator::getSpeakerBufferFill() const
{
  if (speaker_ && speaker_->isInitialized())
  {
    return speaker_->getBufferFillPercentage();
  }
  return 0.5f;  // Default to 50% if no speaker
}

const std::array<uint8_t, 65536>& emulator::getMainRAM() const
{
  return ram_->getMainBank();
}

const std::array<uint8_t, 65536>& emulator::getAuxRAM() const
{
  return ram_->getAuxBank();
}

void emulator::initializeVideoTexture(void* device)
{
  if (video_display_)
  {
    video_display_->initializeTexture(device);
  }
}

bool emulator::loadCharacterROM(const std::string& path)
{
  if (video_display_)
  {
    return video_display_->loadCharacterROM(path);
  }
  return false;
}

// Save state file format:
// - Magic number: "A2E\x01" (4 bytes)
// - CPU state: PC(2) + SP(1) + P(1) + A(1) + X(1) + Y(1) = 7 bytes
// - Soft switch state (serialized)
// - Main RAM: 65536 bytes
// - Aux RAM: 65536 bytes

static constexpr uint32_t SAVE_STATE_MAGIC = 0x01453241;  // "A2E\x01" little-endian

bool emulator::saveState(const std::string& path)
{
  if (!cpu_ || !ram_ || !mmu_)
  {
    std::cerr << "Cannot save state: emulator not initialized" << std::endl;
    return false;
  }

  std::ofstream file(path, std::ios::binary);
  if (!file)
  {
    std::cerr << "Failed to open save file: " << path << std::endl;
    return false;
  }

  // Write magic number
  file.write(reinterpret_cast<const char*>(&SAVE_STATE_MAGIC), sizeof(SAVE_STATE_MAGIC));

  // Write CPU state
  uint16_t pc = cpu_->getPC();
  uint8_t sp = cpu_->getSP();
  uint8_t p = cpu_->getP();
  uint8_t a = cpu_->getA();
  uint8_t x = cpu_->getX();
  uint8_t y = cpu_->getY();

  file.write(reinterpret_cast<const char*>(&pc), sizeof(pc));
  file.write(reinterpret_cast<const char*>(&sp), sizeof(sp));
  file.write(reinterpret_cast<const char*>(&p), sizeof(p));
  file.write(reinterpret_cast<const char*>(&a), sizeof(a));
  file.write(reinterpret_cast<const char*>(&x), sizeof(x));
  file.write(reinterpret_cast<const char*>(&y), sizeof(y));

  // Write soft switch state
  Apple2e::SoftSwitchState switches = mmu_->getSoftSwitchState();
  file.write(reinterpret_cast<const char*>(&switches), sizeof(switches));

  // Write RAM banks
  const auto& main_ram = ram_->getMainBank();
  const auto& aux_ram = ram_->getAuxBank();
  file.write(reinterpret_cast<const char*>(main_ram.data()), main_ram.size());
  file.write(reinterpret_cast<const char*>(aux_ram.data()), aux_ram.size());

  if (!file)
  {
    std::cerr << "Error writing save file" << std::endl;
    return false;
  }

  std::cout << "State saved to: " << path << std::endl;
  return true;
}

bool emulator::loadState(const std::string& path)
{
  if (!cpu_ || !ram_ || !mmu_)
  {
    std::cerr << "Cannot load state: emulator not initialized" << std::endl;
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    std::cerr << "Failed to open save file: " << path << std::endl;
    return false;
  }

  // Read and verify magic number
  uint32_t magic = 0;
  file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (magic != SAVE_STATE_MAGIC)
  {
    std::cerr << "Invalid save file format" << std::endl;
    return false;
  }

  // Read CPU state
  uint16_t pc;
  uint8_t sp, p, a, x, y;

  file.read(reinterpret_cast<char*>(&pc), sizeof(pc));
  file.read(reinterpret_cast<char*>(&sp), sizeof(sp));
  file.read(reinterpret_cast<char*>(&p), sizeof(p));
  file.read(reinterpret_cast<char*>(&a), sizeof(a));
  file.read(reinterpret_cast<char*>(&x), sizeof(x));
  file.read(reinterpret_cast<char*>(&y), sizeof(y));

  // Read soft switch state
  Apple2e::SoftSwitchState switches;
  file.read(reinterpret_cast<char*>(&switches), sizeof(switches));

  // Read RAM banks
  auto& main_ram = ram_->getMainBank();
  auto& aux_ram = ram_->getAuxBank();
  file.read(reinterpret_cast<char*>(main_ram.data()), main_ram.size());
  file.read(reinterpret_cast<char*>(aux_ram.data()), aux_ram.size());

  if (!file)
  {
    std::cerr << "Error reading save file" << std::endl;
    return false;
  }

  // Apply CPU state
  cpu_->setPC(pc);
  cpu_->setSP(sp);
  cpu_->setP(p);
  cpu_->setA(a);
  cpu_->setX(x);
  cpu_->setY(y);

  // Apply soft switch state
  mmu_->getSoftSwitchState() = switches;

  // Reset speaker timing to avoid audio glitches
  if (speaker_)
  {
    speaker_->reset();
  }

  std::cout << "State loaded from: " << path << std::endl;
  return true;
}

bool emulator::savedStateExists(const std::string& path)
{
  return std::filesystem::exists(path);
}

void emulator::pause()
{
  exec_state_ = execution_state::PAUSED;
}

void emulator::resume()
{
  exec_state_ = execution_state::RUNNING;
}

void emulator::stepOver()
{
  exec_state_ = execution_state::STEP_OVER;
}

void emulator::stepOut()
{
  exec_state_ = execution_state::STEP_OUT;
  step_out_stack_depth_ = cpu_->getSP();
}

bool emulator::isPaused() const
{
  return exec_state_ == execution_state::PAUSED;
}

execution_state emulator::getExecutionState() const
{
  return exec_state_;
}

breakpoint_manager* emulator::getBreakpointManager()
{
  return breakpoint_mgr_.get();
}

memory_access_tracker* emulator::getAccessTracker()
{
  return access_tracker_.get();
}

Disk2Controller* emulator::getDiskController()
{
  return disk_controller_.get();
}
