#include "emulator/emulator.hpp"
#include <iostream>
#include <iomanip>

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
  void setPC(uint16_t val) { cpu_.setPC(val); }

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

    std::cout << "\nEmulator initialization complete!" << std::endl;
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Emulator initialization failed: " << e.what() << std::endl;
    return false;
  }
}

void emulator::update(float deltaTime)
{
  // On first update, reset speaker to sync with current CPU cycle
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

void emulator::reset()
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

  // Reset first update flag to resync speaker
  first_update_ = true;
}

void emulator::warmReset()
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
