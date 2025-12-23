#include "emulator/mmu.hpp"
#include <iostream>

MMU::MMU(RAM &ram, ROM &rom, Keyboard *keyboard, Speaker *speaker, DiskII *disk_ii)
    : ram_(ram), rom_(rom), keyboard_(keyboard), speaker_(speaker), disk_ii_(disk_ii)
{
  // Initialize soft switches to power-on state
  // All switches should be OFF at reset
  soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
  soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
  soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
  soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
  soft_switches_.col80_mode = false;
  soft_switches_.altchar_mode = false;

  // IIe memory management - all OFF at reset
  soft_switches_.store80 = false;
  soft_switches_.ramrd = false;
  soft_switches_.ramwrt = false;
  soft_switches_.altzp = false;
  soft_switches_.intcxrom = false;
  soft_switches_.slotc3rom = false;

  // Language card - default state
  soft_switches_.lcbank2 = true;
  soft_switches_.lcread = false;
  soft_switches_.lcwrite = false;
  soft_switches_.lcprewrite = false;

  // Legacy compatibility
  soft_switches_.read_bank = Apple2e::MemoryBank::MAIN;
  soft_switches_.write_bank = Apple2e::MemoryBank::MAIN;
  soft_switches_.keyboard_strobe = false;
}

uint8_t MMU::read(uint16_t address)
{
  // Zero page and stack ($0000-$01FF) - affected by ALTZP
  if (address < 0x0200)
  {
    return ram_.readDirect(address, soft_switches_.altzp);
  }

  // Main RAM area ($0200-$BFFF)
  if (address >= 0x0200 && address < Apple2e::MEM_IO_START)
  {
    // Determine which RAM bank to use for this read
    // Default: use RAMRD flag
    bool use_aux = soft_switches_.ramrd;

    // 80STORE overrides RAMRD for text page 1 and (if HIRES) hires page 1
    // When 80STORE is on, PAGE2 controls main/aux for these display memory areas
    // This is true regardless of whether 80-column VIDEO mode is active
    if (soft_switches_.store80)
    {
      bool is_text_page1 = (address >= Apple2e::MEM_TEXT_PAGE1_START &&
                            address <= Apple2e::MEM_TEXT_PAGE1_END);
      bool is_hires_page1 = soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES &&
                            (address >= Apple2e::MEM_HIRES_PAGE1_START &&
                             address <= Apple2e::MEM_HIRES_PAGE1_END);

      if (is_text_page1 || is_hires_page1)
      {
        // PAGE2 selects aux (true) or main (false) for display memory
        use_aux = (soft_switches_.page_select == Apple2e::PageSelect::PAGE2);
      }
    }

    return ram_.readDirect(address, use_aux);
  }

  // I/O space ($C000-$C0FF)
  if (address >= Apple2e::MEM_IO_START && address <= Apple2e::MEM_IO_END)
  {
    return readSoftSwitch(address);
  }

  // Expansion ROM area ($C100-$CFFF)
  if (address >= Apple2e::MEM_EXPANSION_START && address <= Apple2e::MEM_EXPANSION_END)
  {
    // Handle INTC8ROM mechanism for 80-column card firmware
    // Reading $CFFF clears INTC8ROM (resets to no slot selected for $C800 space)
    if (address == 0xCFFF)
    {
      soft_switches_.intc8rom = false;
    }

    // Accessing $C300-$C3FF enables INTC8ROM (internal ROM at $C800-$CFFF)
    // This allows the 80-column firmware to use the $C800-$CFFF area
    if (address >= 0xC300 && address <= 0xC3FF && !soft_switches_.slotc3rom)
    {
      soft_switches_.intc8rom = true;
    }


    // For all expansion ROM addresses, return internal ROM
    // (No peripheral cards are emulated, so internal ROM is always used)
    return rom_.readExpansionROM(address);
  }

  // Language card / ROM area ($D000-$FFFF)
  if (address >= Apple2e::MEM_ROM_START && address <= Apple2e::MEM_ROM_END)
  {
    if (soft_switches_.lcread)
    {
      // Read from language card RAM
      // TODO: Implement LC RAM banks
      return ram_.readDirect(address, soft_switches_.altzp);
    }
    else
    {
      // Read from ROM
      return rom_.read(address);
    }
  }

  // Unmapped address
  return 0xFF;
}

void MMU::write(uint16_t address, uint8_t value)
{
  // Zero page and stack ($0000-$01FF) - affected by ALTZP
  if (address < 0x0200)
  {
    ram_.writeDirect(address, value, soft_switches_.altzp);

    // We no longer monitor CSW for 80-column mode changes.
    // The CSW monitoring approach was problematic because:
    // 1. The scroll routine temporarily modifies zero page during operation
    // 2. We were incorrectly clearing memory banking switches at wrong times
    // 
    // Instead, we rely on the ROM properly accessing CLR80VID ($C00C) to
    // exit 80-column mode, which is handled in readSoftSwitch/writeSoftSwitch.
    return;
  }

  // Main RAM area ($0200-$BFFF)
  if (address >= 0x0200 && address < Apple2e::MEM_IO_START)
  {
    // Determine which RAM bank to use for this write
    // Default: use RAMWRT flag
    bool use_aux = soft_switches_.ramwrt;

    // 80STORE overrides RAMWRT for text page 1 and (if HIRES) hires page 1
    // When 80STORE is on, PAGE2 controls main/aux for these display memory areas
    // This is true regardless of whether 80-column VIDEO mode is active
    if (soft_switches_.store80)
    {
      bool is_text_page1 = (address >= Apple2e::MEM_TEXT_PAGE1_START &&
                            address <= Apple2e::MEM_TEXT_PAGE1_END);
      bool is_hires_page1 = soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES &&
                            (address >= Apple2e::MEM_HIRES_PAGE1_START &&
                             address <= Apple2e::MEM_HIRES_PAGE1_END);

      if (is_text_page1 || is_hires_page1)
      {
        // PAGE2 selects aux (true) or main (false) for display memory
        use_aux = (soft_switches_.page_select == Apple2e::PageSelect::PAGE2);
      }
    }
    
    ram_.writeDirect(address, value, use_aux);
    return;
  }

  // I/O space ($C000-$C0FF)
  if (address >= Apple2e::MEM_IO_START && address <= Apple2e::MEM_IO_END)
  {
    writeSoftSwitch(address, value);
    return;
  }

  // Expansion ROM area ($C100-$CFFF) - writes ignored
  if (address >= Apple2e::MEM_EXPANSION_START && address <= Apple2e::MEM_EXPANSION_END)
  {
    return;
  }

  // Language card area ($D000-$FFFF)
  if (address >= Apple2e::MEM_ROM_START && address <= Apple2e::MEM_ROM_END)
  {
    if (soft_switches_.lcwrite)
    {
      // Write to language card RAM
      ram_.writeDirect(address, value, soft_switches_.altzp);
    }
    // ROM writes are ignored
    return;
  }
}

uint8_t MMU::peek(uint16_t address) const
{
  // Peek reads memory without triggering any side effects
  // Used by debuggers, memory viewers, etc.

  // Zero page and stack ($0000-$01FF) - affected by ALTZP
  if (address < 0x0200)
  {
    return ram_.readDirect(address, soft_switches_.altzp);
  }

  // Main RAM area ($0200-$BFFF)
  if (address >= 0x0200 && address < Apple2e::MEM_IO_START)
  {
    bool use_aux = soft_switches_.ramrd;

    if (soft_switches_.store80)
    {
      bool is_text_page1 = (address >= Apple2e::MEM_TEXT_PAGE1_START &&
                            address <= Apple2e::MEM_TEXT_PAGE1_END);
      bool is_hires_page1 = soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES &&
                            (address >= Apple2e::MEM_HIRES_PAGE1_START &&
                             address <= Apple2e::MEM_HIRES_PAGE1_END);

      if (is_text_page1 || is_hires_page1)
      {
        use_aux = (soft_switches_.page_select == Apple2e::PageSelect::PAGE2);
      }
    }

    return ram_.readDirect(address, use_aux);
  }

  // I/O space ($C000-$C0FF) - return current state without side effects
  if (address >= Apple2e::MEM_IO_START && address <= Apple2e::MEM_IO_END)
  {
    // For keyboard, just return the latched value
    if (address == Apple2e::KBD)
    {
      if (keyboard_)
      {
        // Peek at keyboard without clearing strobe
        return keyboard_->read(address);
      }
      return 0x00;
    }

    // For KBDSTRB, return any-key-down status without clearing
    if (address == Apple2e::KBDSTRB)
    {
      if (keyboard_)
      {
        // This still triggers side effect in keyboard - but keyboard peek would be needed
        // For now, just return 0 to avoid side effects
        return 0x00;
      }
      return 0x00;
    }

    // Speaker - return 0 without toggling
    if (address == Apple2e::SPKR)
    {
      return 0x00;
    }

    // Status reads are safe (no side effects)
    switch (address)
    {
      case Apple2e::RDLCBNK2:
        return soft_switches_.lcbank2 ? 0x80 : 0x00;
      case Apple2e::RDLCRAM:
        return soft_switches_.lcread ? 0x80 : 0x00;
      case Apple2e::RDRAMRD:
        return soft_switches_.ramrd ? 0x80 : 0x00;
      case Apple2e::RDRAMWRT:
        return soft_switches_.ramwrt ? 0x80 : 0x00;
      case Apple2e::RDCXROM:
        return soft_switches_.intcxrom ? 0x80 : 0x00;
      case Apple2e::RDALTZP:
        return soft_switches_.altzp ? 0x80 : 0x00;
      case Apple2e::RDC3ROM:
        return soft_switches_.slotc3rom ? 0x80 : 0x00;
      case Apple2e::RD80STORE:
        return soft_switches_.store80 ? 0x80 : 0x00;
      case Apple2e::RDTEXT:
        return soft_switches_.video_mode == Apple2e::VideoMode::TEXT ? 0x80 : 0x00;
      case Apple2e::RDMIXED:
        return soft_switches_.screen_mode == Apple2e::ScreenMode::MIXED ? 0x80 : 0x00;
      case Apple2e::RDPAGE2:
        return soft_switches_.page_select == Apple2e::PageSelect::PAGE2 ? 0x80 : 0x00;
      case Apple2e::RDHIRES:
        return soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES ? 0x80 : 0x00;
      case Apple2e::RDALTCHAR:
        return soft_switches_.altchar_mode ? 0x80 : 0x00;
      case Apple2e::RD80VID:
        return soft_switches_.col80_mode ? 0x80 : 0x00;
      default:
        return 0x00;
    }
  }

  // Expansion ROM area ($C100-$CFFF) - read without INTC8ROM side effects
  if (address >= Apple2e::MEM_EXPANSION_START && address <= Apple2e::MEM_EXPANSION_END)
  {
    return rom_.readExpansionROM(address);
  }

  // Language card / ROM area ($D000-$FFFF)
  if (address >= Apple2e::MEM_ROM_START && address <= Apple2e::MEM_ROM_END)
  {
    if (soft_switches_.lcread)
    {
      return ram_.readDirect(address, soft_switches_.altzp);
    }
    else
    {
      return rom_.read(address);
    }
  }

  return 0xFF;
}

AddressRange MMU::getAddressRange() const
{
  // MMU handles the entire address space
  return {0x0000, 0xFFFF};
}

Apple2e::SoftSwitchState MMU::getSoftSwitchSnapshot() const
{
  // Create a copy of the current state
  Apple2e::SoftSwitchState snapshot = soft_switches_;
  
  // Read CSW (Character output Switch Vector) from $36-$37
  // Always read from main RAM for consistency (these vectors are in main ZP)
  uint8_t csw_lo = ram_.readDirect(0x0036, false);
  uint8_t csw_hi = ram_.readDirect(0x0037, false);
  snapshot.csw = csw_lo | (csw_hi << 8);
  
  // Read KSW (Keyboard input Switch Vector) from $38-$39
  uint8_t ksw_lo = ram_.readDirect(0x0038, false);
  uint8_t ksw_hi = ram_.readDirect(0x0039, false);
  snapshot.ksw = ksw_lo | (ksw_hi << 8);
  
  return snapshot;
}

std::string MMU::getName() const
{
  return "MMU";
}

uint8_t MMU::readSoftSwitch(uint16_t address)
{
  // Keyboard data
  if (address == Apple2e::KBD)
  {
    if (keyboard_)
    {
      return keyboard_->read(address);
    }
    return 0x00;
  }

  // Keyboard strobe clear
  if (address == Apple2e::KBDSTRB)
  {
    if (keyboard_)
    {
      return keyboard_->read(address);
    }
    return 0x00;
  }

  // Status read addresses - return bit 7 set if condition is true
  switch (address)
  {
    case Apple2e::RDLCBNK2:
      return soft_switches_.lcbank2 ? 0x80 : 0x00;

    case Apple2e::RDLCRAM:
      return soft_switches_.lcread ? 0x80 : 0x00;

    case Apple2e::RDRAMRD:
      return soft_switches_.ramrd ? 0x80 : 0x00;

    case Apple2e::RDRAMWRT:
      return soft_switches_.ramwrt ? 0x80 : 0x00;

    case Apple2e::RDCXROM:
      return soft_switches_.intcxrom ? 0x80 : 0x00;

    case Apple2e::RDALTZP:
      return soft_switches_.altzp ? 0x80 : 0x00;

    case Apple2e::RDC3ROM:
      return soft_switches_.slotc3rom ? 0x80 : 0x00;

    case Apple2e::RD80STORE:
      return soft_switches_.store80 ? 0x80 : 0x00;

    case Apple2e::RDVBLBAR:
      // VBL status - for now return not in VBL
      return 0x80;

    case Apple2e::RDTEXT:
      return (soft_switches_.video_mode == Apple2e::VideoMode::TEXT) ? 0x80 : 0x00;

    case Apple2e::RDMIXED:
      return (soft_switches_.screen_mode == Apple2e::ScreenMode::MIXED) ? 0x80 : 0x00;

    case Apple2e::RDPAGE2:
      return (soft_switches_.page_select == Apple2e::PageSelect::PAGE2) ? 0x80 : 0x00;

    case Apple2e::RDHIRES:
      return (soft_switches_.graphics_mode == Apple2e::GraphicsMode::HIRES) ? 0x80 : 0x00;

    case Apple2e::RDALTCHAR:
      return soft_switches_.altchar_mode ? 0x80 : 0x00;

    case Apple2e::RD80VID:
      return soft_switches_.col80_mode ? 0x80 : 0x00;

    // IIe memory/video switches - reading also activates them
    case Apple2e::CLR80STORE:
      soft_switches_.store80 = false;
      return 0x00;
      
    case Apple2e::SET80STORE:
      soft_switches_.store80 = true;
      return 0x00;
      
    case Apple2e::RDMAINRAM:
      soft_switches_.ramrd = false;
      return 0x00;
      
    case Apple2e::RDCARDRAM:
      soft_switches_.ramrd = true;
      return 0x00;
      
    case Apple2e::WRMAINRAM:
      soft_switches_.ramwrt = false;
      return 0x00;
      
    case Apple2e::WRCARDRAM:
      soft_switches_.ramwrt = true;
      return 0x00;
      
    case Apple2e::SETSTDZP:
      soft_switches_.altzp = false;
      return 0x00;
      
    case Apple2e::SETALTZP:
      soft_switches_.altzp = true;
      return 0x00;
      
    case Apple2e::CLR80VID:
      soft_switches_.col80_mode = false;
      soft_switches_.store80 = false;
      soft_switches_.ramrd = false;
      soft_switches_.ramwrt = false;
      return 0x00;
      
    case Apple2e::SET80VID:
      soft_switches_.col80_mode = true;
      return 0x00;

    // Video mode switches - reading also activates them
    case Apple2e::TXTCLR:
      soft_switches_.video_mode = Apple2e::VideoMode::GRAPHICS;
      return 0x00;

    case Apple2e::TXTSET:
      soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
      return 0x00;

    case Apple2e::MIXCLR:
      soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
      return 0x00;

    case Apple2e::MIXSET:
      soft_switches_.screen_mode = Apple2e::ScreenMode::MIXED;
      return 0x00;

    case Apple2e::TXTPAGE1:
      soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
      return 0x00;

    case Apple2e::TXTPAGE2:
      soft_switches_.page_select = Apple2e::PageSelect::PAGE2;
      return 0x00;

    case Apple2e::LORES:
      soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
      return 0x00;

    case Apple2e::HIRES:
      soft_switches_.graphics_mode = Apple2e::GraphicsMode::HIRES;
      return 0x00;

    // Speaker
    case Apple2e::SPKR:
      // Toggle speaker
      if (speaker_)
      {
        speaker_->toggle(cycle_count_);
      }
      return 0x00;

    // Game I/O
    case Apple2e::RDBTN0:
    case Apple2e::RDBTN1:
    case Apple2e::RDBTN2:
      // Buttons not pressed
      return 0x00;

    case Apple2e::PADDL0:
    case Apple2e::PADDL1:
    case Apple2e::PADDL2:
    case Apple2e::PADDL3:
      // Paddles - return timing expired
      return 0x00;

    case Apple2e::PTRIG:
      // Reset paddle timers
      return 0x00;

    // Annunciators - reading returns floating bus
    case Apple2e::CLRAN0:
    case Apple2e::SETAN0:
    case Apple2e::CLRAN1:
    case Apple2e::SETAN1:
    case Apple2e::CLRAN2:
    case Apple2e::SETAN2:
    case Apple2e::CLRAN3:
    case Apple2e::SETAN3:
      return 0x00;

    default:
      break;
  }

  // Language card switches ($C080-$C08F)
  if (address >= 0xC080 && address <= 0xC08F)
  {
    handleLanguageCard(address);
    return 0x00;
  }

  // Disk II controller (Slot 6: $C0E0-$C0EF)
  if (address >= 0xC0E0 && address <= 0xC0EF)
  {
    if (disk_ii_)
    {
      // Update disk timing before read so it knows current cycle
      disk_ii_->setCycleCount(cycle_count_);
      return disk_ii_->read(address);
    }
    return 0xFF;  // Return 0xFF for empty slot
  }

  // Unknown switch - return floating bus (approximate with 0)
  return 0x00;
}

void MMU::writeSoftSwitch(uint16_t address, uint8_t value)
{
  (void)value; // Most soft switches ignore the written value

  switch (address)
  {
    // Memory management switches
    case Apple2e::CLR80STORE:
      soft_switches_.store80 = false;
      break;

    case Apple2e::SET80STORE:
      soft_switches_.store80 = true;
      break;

    case Apple2e::RDMAINRAM:
      soft_switches_.ramrd = false;
      break;

    case Apple2e::RDCARDRAM:
      soft_switches_.ramrd = true;
      break;

    case Apple2e::WRMAINRAM:
      soft_switches_.ramwrt = false;
      break;

    case Apple2e::WRCARDRAM:
      soft_switches_.ramwrt = true;
      break;

    case Apple2e::SETSLOTCXROM:
      soft_switches_.intcxrom = false;
      break;

    case Apple2e::SETINTCXROM:
      soft_switches_.intcxrom = true;
      break;

    case Apple2e::SETSTDZP:
      soft_switches_.altzp = false;
      break;

    case Apple2e::SETALTZP:
      soft_switches_.altzp = true;
      break;

    case Apple2e::SETINTC3ROM:
      soft_switches_.slotc3rom = false;
      break;

    case Apple2e::SETSLOTC3ROM:
      soft_switches_.slotc3rom = true;
      break;

    // Video switches
    case Apple2e::CLR80VID:
      // When leaving 80-column mode, we should also reset the memory banking
      // switches that the 80-column firmware uses. The ROM may not do this
      // explicitly, so we handle it here to ensure proper 40-column operation.
      soft_switches_.col80_mode = false;
      soft_switches_.store80 = false;
      soft_switches_.ramrd = false;
      soft_switches_.ramwrt = false;
      // Note: We intentionally do NOT clear altzp here as it may be used
      // independently of 80-column mode by other software
      break;

    case Apple2e::SET80VID:
      soft_switches_.col80_mode = true;
      break;

    case Apple2e::CLRALTCHAR:
      soft_switches_.altchar_mode = false;
      break;

    case Apple2e::SETALTCHAR:
      soft_switches_.altchar_mode = true;
      break;

    // Keyboard strobe
    case Apple2e::KBDSTRB:
      if (keyboard_)
      {
        keyboard_->write(address, value);
      }
      break;

    // Video mode switches - writing also activates them
    case Apple2e::TXTCLR:
      soft_switches_.video_mode = Apple2e::VideoMode::GRAPHICS;
      break;

    case Apple2e::TXTSET:
      soft_switches_.video_mode = Apple2e::VideoMode::TEXT;
      break;

    case Apple2e::MIXCLR:
      soft_switches_.screen_mode = Apple2e::ScreenMode::FULL;
      break;

    case Apple2e::MIXSET:
      soft_switches_.screen_mode = Apple2e::ScreenMode::MIXED;
      break;

    case Apple2e::TXTPAGE1:
      soft_switches_.page_select = Apple2e::PageSelect::PAGE1;
      break;

    case Apple2e::TXTPAGE2:
      soft_switches_.page_select = Apple2e::PageSelect::PAGE2;
      break;

    case Apple2e::LORES:
      soft_switches_.graphics_mode = Apple2e::GraphicsMode::LORES;
      break;

    case Apple2e::HIRES:
      soft_switches_.graphics_mode = Apple2e::GraphicsMode::HIRES;
      break;

    default:
      // Language card switches ($C080-$C08F)
      if (address >= 0xC080 && address <= 0xC08F)
      {
        handleLanguageCard(address);
      }
      // Disk II controller (Slot 6: $C0E0-$C0EF)
      else if (address >= 0xC0E0 && address <= 0xC0EF)
      {
        if (disk_ii_)
        {
          // Update disk timing before write so it knows current cycle
          disk_ii_->setCycleCount(cycle_count_);
          disk_ii_->write(address, value);
        }
      }
      break;
  }
}

void MMU::handleLanguageCard(uint16_t address)
{
  // Language card soft switches control:
  // - Which LC RAM bank is active ($D000-$DFFF): bank 1 or bank 2
  // - Whether reads come from LC RAM or ROM
  // - Whether writes go to LC RAM
  //
  // The switches at $C080-$C087 select bank 2
  // The switches at $C088-$C08F select bank 1
  //
  // Bit 0 of address: 0=read ROM, 1=read RAM (with pre-read)
  // Bit 1 of address: 0=write disabled, 1=write enable possible
  // Bit 3 of address: 0=bank 2, 1=bank 1

  // Determine bank
  soft_switches_.lcbank2 = (address & 0x08) == 0;

  // Get the operation type (bits 0-2, ignoring bit 3)
  uint8_t op = address & 0x03;

  switch (op)
  {
    case 0: // $C080, $C084, $C088, $C08C: Read ROM, write disabled
      soft_switches_.lcread = false;
      soft_switches_.lcwrite = false;
      soft_switches_.lcprewrite = false;
      break;

    case 1: // $C081, $C085, $C089, $C08D: Read ROM, write enable on second read
      soft_switches_.lcread = false;
      if (soft_switches_.lcprewrite)
      {
        soft_switches_.lcwrite = true;
      }
      soft_switches_.lcprewrite = true;
      break;

    case 2: // $C082, $C086, $C08A, $C08E: Read ROM, write disabled
      soft_switches_.lcread = false;
      soft_switches_.lcwrite = false;
      soft_switches_.lcprewrite = false;
      break;

    case 3: // $C083, $C087, $C08B, $C08F: Read RAM, write enable on second read
      soft_switches_.lcread = true;
      if (soft_switches_.lcprewrite)
      {
        soft_switches_.lcwrite = true;
      }
      soft_switches_.lcprewrite = true;
      break;
  }
}

void MMU::handleBankSwitch(uint16_t address)
{
  // Legacy bank switching - redirect to language card handler
  handleLanguageCard(address);
}
