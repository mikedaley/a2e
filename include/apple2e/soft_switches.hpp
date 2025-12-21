#pragma once

#include <cstdint>

/**
 * Apple IIe Soft Switches
 *
 * Soft switches are memory-mapped I/O locations that control hardware behavior.
 * Reading from these addresses returns status, writing toggles the switch.
 */
namespace Apple2e
{

// =============================================================================
// Keyboard
// =============================================================================
constexpr uint16_t KBD = 0xC000;           // Read: Last key pressed + 128
constexpr uint16_t KBDSTRB = 0xC010;       // Read/Write: Clear keyboard strobe

// Aliases for compatibility
constexpr uint16_t KB_DATA = KBD;
constexpr uint16_t KB_STROBE = KBDSTRB;

// =============================================================================
// Memory Management Soft Switches (IIe specific)
// =============================================================================

// 80STORE - When ON, PAGE2 ($C054/$C055) switches main/aux for display memory
constexpr uint16_t CLR80STORE = 0xC000;    // Write: 80STORE off (PAGE2 controls video page)
constexpr uint16_t SET80STORE = 0xC001;    // Write: 80STORE on (PAGE2 controls main/aux)

// RAMRD - Controls reading from $0200-$BFFF (when 80STORE is off)
constexpr uint16_t RDMAINRAM = 0xC002;     // Write: Read from main RAM
constexpr uint16_t RDCARDRAM = 0xC003;     // Write: Read from aux RAM

// RAMWRT - Controls writing to $0200-$BFFF (when 80STORE is off)
constexpr uint16_t WRMAINRAM = 0xC004;     // Write: Write to main RAM
constexpr uint16_t WRCARDRAM = 0xC005;     // Write: Write to aux RAM

// INTCXROM - Controls $C100-$CFFF ROM source
constexpr uint16_t SETSLOTCXROM = 0xC006;  // Write: Slot ROM at $C100-$CFFF
constexpr uint16_t SETINTCXROM = 0xC007;   // Write: Internal ROM at $C100-$CFFF

// ALTZP - Controls zero page and stack bank ($0000-$01FF)
constexpr uint16_t SETSTDZP = 0xC008;      // Write: Main zero page and stack
constexpr uint16_t SETALTZP = 0xC009;      // Write: Aux zero page and stack

// SLOTC3ROM - Controls slot 3 ROM
constexpr uint16_t SETINTC3ROM = 0xC00A;   // Write: Internal ROM at $C300-$C3FF
constexpr uint16_t SETSLOTC3ROM = 0xC00B;  // Write: Slot ROM at $C300-$C3FF

// =============================================================================
// Video Soft Switches (IIe specific)
// =============================================================================

// 80COL - 40/80 column display
constexpr uint16_t CLR80VID = 0xC00C;      // Write: 40 column mode
constexpr uint16_t SET80VID = 0xC00D;      // Write: 80 column mode

// ALTCHAR - Character set selection
constexpr uint16_t CLRALTCHAR = 0xC00E;    // Write: Primary character set
constexpr uint16_t SETALTCHAR = 0xC00F;    // Write: Alternate character set

// =============================================================================
// Status Read Addresses (Read bit 7 for status)
// =============================================================================
constexpr uint16_t RDLCBNK2 = 0xC011;      // Read: LC bank 2 status (1=bank2, 0=bank1)
constexpr uint16_t RDLCRAM = 0xC012;       // Read: LC RAM status (1=RAM, 0=ROM)
constexpr uint16_t RDRAMRD = 0xC013;       // Read: RAMRD status (1=aux, 0=main)
constexpr uint16_t RDRAMWRT = 0xC014;      // Read: RAMWRT status (1=aux, 0=main)
constexpr uint16_t RDCXROM = 0xC015;       // Read: INTCXROM status (1=internal, 0=slot)
constexpr uint16_t RDALTZP = 0xC016;       // Read: ALTZP status (1=aux, 0=main)
constexpr uint16_t RDC3ROM = 0xC017;       // Read: SLOTC3ROM status (1=slot, 0=internal)
constexpr uint16_t RD80STORE = 0xC018;     // Read: 80STORE status (1=on, 0=off)
constexpr uint16_t RDVBLBAR = 0xC019;      // Read: VBL status (active low)
constexpr uint16_t RDTEXT = 0xC01A;        // Read: TEXT status (1=text, 0=graphics)
constexpr uint16_t RDMIXED = 0xC01B;       // Read: MIXED status (1=mixed, 0=full)
constexpr uint16_t RDPAGE2 = 0xC01C;       // Read: PAGE2 status (1=page2, 0=page1)
constexpr uint16_t RDHIRES = 0xC01D;       // Read: HIRES status (1=hires, 0=lores)
constexpr uint16_t RDALTCHAR = 0xC01E;     // Read: ALTCHAR status (1=alt, 0=primary)
constexpr uint16_t RD80VID = 0xC01F;       // Read: 80VID status (1=80col, 0=40col)

// =============================================================================
// Speaker
// =============================================================================
constexpr uint16_t SPKR = 0xC030;          // Read: Toggle speaker

// =============================================================================
// Game I/O
// =============================================================================
constexpr uint16_t STROBE = 0xC040;        // Read: Game I/O strobe
constexpr uint16_t TAPEIN = 0xC060;        // Read bit 7: Cassette input
constexpr uint16_t RDBTN0 = 0xC061;        // Read bit 7: Button 0 / Open Apple
constexpr uint16_t RDBTN1 = 0xC062;        // Read bit 7: Button 1 / Solid Apple
constexpr uint16_t RDBTN2 = 0xC063;        // Read bit 7: Button 2 / Shift key
constexpr uint16_t PADDL0 = 0xC064;        // Read bit 7: Paddle 0
constexpr uint16_t PADDL1 = 0xC065;        // Read bit 7: Paddle 1
constexpr uint16_t PADDL2 = 0xC066;        // Read bit 7: Paddle 2
constexpr uint16_t PADDL3 = 0xC067;        // Read bit 7: Paddle 3
constexpr uint16_t PTRIG = 0xC070;         // Read: Reset paddle timers

// =============================================================================
// Video Mode Soft Switches (original Apple II compatible)
// =============================================================================
constexpr uint16_t TXTCLR = 0xC050;        // Read/Write: Graphics mode
constexpr uint16_t TXTSET = 0xC051;        // Read/Write: Text mode
constexpr uint16_t MIXCLR = 0xC052;        // Read/Write: Full screen
constexpr uint16_t MIXSET = 0xC053;        // Read/Write: Mixed mode
constexpr uint16_t TXTPAGE1 = 0xC054;      // Read/Write: Display page 1
constexpr uint16_t TXTPAGE2 = 0xC055;      // Read/Write: Display page 2 (or aux mem if 80STORE)
constexpr uint16_t LORES = 0xC056;         // Read/Write: Lo-res graphics
constexpr uint16_t HIRES = 0xC057;         // Read/Write: Hi-res graphics

// Aliases for compatibility
constexpr uint16_t SW_TEXT_MODE = TXTCLR;
constexpr uint16_t SW_GRAPHICS_MODE = TXTSET;
constexpr uint16_t SW_FULL_SCREEN = MIXCLR;
constexpr uint16_t SW_MIXED_MODE = MIXSET;
constexpr uint16_t SW_PAGE1 = TXTPAGE1;
constexpr uint16_t SW_PAGE2 = TXTPAGE2;
constexpr uint16_t SW_LORES = LORES;
constexpr uint16_t SW_HIRES = HIRES;

// =============================================================================
// Annunciators
// =============================================================================
constexpr uint16_t CLRAN0 = 0xC058;        // Read/Write: Annunciator 0 off
constexpr uint16_t SETAN0 = 0xC059;        // Read/Write: Annunciator 0 on
constexpr uint16_t CLRAN1 = 0xC05A;        // Read/Write: Annunciator 1 off
constexpr uint16_t SETAN1 = 0xC05B;        // Read/Write: Annunciator 1 on
constexpr uint16_t CLRAN2 = 0xC05C;        // Read/Write: Annunciator 2 off
constexpr uint16_t SETAN2 = 0xC05D;        // Read/Write: Annunciator 2 on
constexpr uint16_t CLRAN3 = 0xC05E;        // Read/Write: Annunciator 3 off
constexpr uint16_t SETAN3 = 0xC05F;        // Read/Write: Annunciator 3 on

// =============================================================================
// Language Card / Bank Switching ($C080-$C08F)
// =============================================================================
// Bank 2 ($D000-$DFFF uses LC bank 2)
constexpr uint16_t LCBANK2_RDROM_NOWR = 0xC080;    // Read ROM, no write
constexpr uint16_t LCBANK2_RDROM_WREN = 0xC081;    // Read ROM, write enable (2 reads)
constexpr uint16_t LCBANK2_RDROM_NOWR2 = 0xC082;   // Read ROM, no write
constexpr uint16_t LCBANK2_RDWR = 0xC083;          // Read/write RAM (2 reads)
constexpr uint16_t LCBANK2_RDROM_NOWR3 = 0xC084;   // Same as $C080
constexpr uint16_t LCBANK2_RDROM_WREN2 = 0xC085;   // Same as $C081
constexpr uint16_t LCBANK2_RDROM_NOWR4 = 0xC086;   // Same as $C082
constexpr uint16_t LCBANK2_RDWR2 = 0xC087;         // Same as $C083

// Bank 1 ($D000-$DFFF uses LC bank 1)
constexpr uint16_t LCBANK1_RDROM_NOWR = 0xC088;    // Read ROM, no write
constexpr uint16_t LCBANK1_RDROM_WREN = 0xC089;    // Read ROM, write enable (2 reads)
constexpr uint16_t LCBANK1_RDROM_NOWR2 = 0xC08A;   // Read ROM, no write
constexpr uint16_t LCBANK1_RDWR = 0xC08B;          // Read/write RAM (2 reads)
constexpr uint16_t LCBANK1_RDROM_NOWR3 = 0xC08C;   // Same as $C088
constexpr uint16_t LCBANK1_RDROM_WREN2 = 0xC08D;   // Same as $C089
constexpr uint16_t LCBANK1_RDROM_NOWR4 = 0xC08E;   // Same as $C08A
constexpr uint16_t LCBANK1_RDWR2 = 0xC08F;         // Same as $C08B

// Legacy aliases for bank switching
constexpr uint16_t BANK_READ_MAIN = 0xC080;
constexpr uint16_t BANK_READ_AUX = 0xC081;
constexpr uint16_t BANK_WRITE_MAIN = 0xC082;
constexpr uint16_t BANK_WRITE_AUX = 0xC083;
constexpr uint16_t BANK_READ_MAIN_WRITE_AUX = 0xC084;
constexpr uint16_t BANK_READ_AUX_WRITE_MAIN = 0xC085;
constexpr uint16_t BANK_READ_MAIN_WRITE_MAIN = 0xC086;
constexpr uint16_t BANK_READ_AUX_WRITE_AUX = 0xC087;

// =============================================================================
// Soft switch state flags
// =============================================================================

enum class VideoMode : uint8_t
{
  TEXT = 0,
  GRAPHICS = 1
};

enum class ScreenMode : uint8_t
{
  FULL = 0,
  MIXED = 1
};

enum class PageSelect : uint8_t
{
  PAGE1 = 0,
  PAGE2 = 1
};

enum class GraphicsMode : uint8_t
{
  LORES = 0,
  HIRES = 1
};

enum class MemoryBank : uint8_t
{
  MAIN = 0,
  AUX = 1
};

/**
 * SoftSwitchState - Tracks the current state of all soft switches
 */
struct SoftSwitchState
{
  // Video switches
  VideoMode video_mode = VideoMode::TEXT;
  ScreenMode screen_mode = ScreenMode::FULL;
  PageSelect page_select = PageSelect::PAGE1;
  GraphicsMode graphics_mode = GraphicsMode::LORES;
  bool col80_mode = false;           // 80-column mode
  bool altchar_mode = false;         // Alternate character set
  
  // Memory management switches (IIe specific)
  bool store80 = false;              // 80STORE: PAGE2 switches aux mem for display
  bool ramrd = false;                // RAMRD: Read from aux RAM ($0200-$BFFF)
  bool ramwrt = false;               // RAMWRT: Write to aux RAM ($0200-$BFFF)
  bool altzp = false;                // ALTZP: Use aux zero page and stack
  bool intcxrom = false;             // INTCXROM: Use internal ROM at $C100-$CFFF
  bool slotc3rom = false;            // SLOTC3ROM: Use slot ROM at $C300-$C3FF
  bool intc8rom = false;             // INTC8ROM: Internal ROM at $C800-$CFFF (auto-set by $C3xx access)
  
  // Language card switches
  bool lcbank2 = true;               // LC bank select: true=bank2, false=bank1
  bool lcread = false;               // LC read: true=RAM, false=ROM
  bool lcwrite = false;              // LC write enable
  bool lcprewrite = false;           // LC pre-write state (for double-read requirement)
  
  // Legacy compatibility
  MemoryBank read_bank = MemoryBank::MAIN;
  MemoryBank write_bank = MemoryBank::MAIN;
  bool keyboard_strobe = false;
};

} // namespace Apple2e
