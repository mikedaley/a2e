/**
 * Language Card and Bank Switching Softswitch Tests
 *
 * This test file validates the behavior of the Apple IIe language card and
 * bank switching softswitches against documented real hardware behavior.
 *
 * References:
 * - Apple IIe Technical Reference Manual
 * - Understanding the Apple IIe by Jim Sather
 *
 * Language Card Soft Switches ($C080-$C08F):
 * - $C080-$C087: Select Bank 2 for $D000-$DFFF
 * - $C088-$C08F: Select Bank 1 for $D000-$DFFF
 *
 * Operation modes (based on bits 0-1 of address):
 * - 0: Read ROM, write disabled
 * - 1: Read ROM, write enabled after two consecutive reads
 * - 2: Read ROM, write disabled
 * - 3: Read RAM, write enabled after two consecutive reads
 */

#include "emulator/mmu.hpp"
#include "emulator/ram.hpp"
#include "emulator/rom.hpp"
#include "apple2e/soft_switches.hpp"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <functional>
#include <vector>
#include <string>

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;
static std::string current_test_name;

#define TEST_CASE(name) \
    current_test_name = name; \
    std::cout << "  Testing: " << name << "... " << std::flush;

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        std::cout << "FAILED" << std::endl; \
        std::cerr << "    Assertion failed: " #condition << std::endl; \
        std::cerr << "    at " << __FILE__ << ":" << __LINE__ << std::endl; \
        tests_failed++; \
        return false; \
    }

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        std::cout << "FAILED" << std::endl; \
        std::cerr << "    Assertion failed: " #expected " == " #actual << std::endl; \
        std::cerr << "    Expected: 0x" << std::hex << static_cast<int>(expected) \
                  << " Actual: 0x" << static_cast<int>(actual) << std::dec << std::endl; \
        std::cerr << "    at " << __FILE__ << ":" << __LINE__ << std::endl; \
        tests_failed++; \
        return false; \
    }

#define TEST_PASS() \
    std::cout << "PASSED" << std::endl; \
    tests_passed++;

/**
 * Test fixture that sets up MMU with RAM and ROM
 */
class LanguageCardTestFixture
{
public:
    RAM ram;
    ROM rom;
    MMU mmu;

    LanguageCardTestFixture() : mmu(ram, rom)
    {
        // Initialize ROM with a known pattern (0xEE for easy identification)
        auto& romData = rom.getData();
        std::fill(romData.begin(), romData.end(), 0xEE);

        // Initialize main RAM in the language card area with a different pattern
        // Bank 2 is at $D000-$FFFF in main RAM
        auto& mainRam = ram.getMainBank();
        for (uint32_t addr = 0xD000; addr <= 0xFFFF; addr++)
        {
            mainRam[addr] = 0xD2;  // 'D2' for bank 2
        }
        // Bank 1 is stored at $C000-$CFFF (offset -0x1000 from $D000-$DFFF)
        for (uint32_t addr = 0xC000; addr <= 0xCFFF; addr++)
        {
            mainRam[addr] = 0xD1;  // 'D1' for bank 1
        }
    }

    /**
     * Reset soft switches to power-on state
     */
    void reset()
    {
        auto& ss = mmu.getSoftSwitchState();
        ss.lcbank2 = true;
        ss.lcread = false;
        ss.lcwrite = false;
        ss.lcprewrite = false;
    }

    /**
     * Helper to read a softswitch (triggers side effects)
     */
    uint8_t readSwitch(uint16_t addr)
    {
        return mmu.read(addr);
    }

    /**
     * Helper to write to a softswitch
     */
    void writeSwitch(uint16_t addr, uint8_t value = 0)
    {
        mmu.write(addr, value);
    }

    /**
     * Get soft switch state reference
     */
    Apple2e::SoftSwitchState& state()
    {
        return mmu.getSoftSwitchState();
    }
};

// ============================================================================
// Test: Initial State
// ============================================================================
bool test_initial_state()
{
    TEST_CASE("Initial power-on state");
    LanguageCardTestFixture f;

    // At power-on, language card should be configured to read ROM
    ASSERT_TRUE(f.state().lcbank2);      // Bank 2 selected
    ASSERT_FALSE(f.state().lcread);      // Read from ROM (not RAM)
    ASSERT_FALSE(f.state().lcwrite);     // Write disabled
    ASSERT_FALSE(f.state().lcprewrite);  // Pre-write not pending

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Bank Selection
// ============================================================================
bool test_bank_selection_bank2()
{
    TEST_CASE("$C080-$C087 select Bank 2");
    LanguageCardTestFixture f;

    // Start with bank 1 selected
    f.readSwitch(0xC088);  // Select bank 1
    ASSERT_FALSE(f.state().lcbank2);

    // Each of $C080-$C087 should select bank 2
    for (uint16_t addr = 0xC080; addr <= 0xC087; addr++)
    {
        f.readSwitch(0xC088);  // Reset to bank 1
        f.readSwitch(addr);
        ASSERT_TRUE(f.state().lcbank2);
    }

    TEST_PASS();
    return true;
}

bool test_bank_selection_bank1()
{
    TEST_CASE("$C088-$C08F select Bank 1");
    LanguageCardTestFixture f;

    // Start with bank 2 selected (default)
    ASSERT_TRUE(f.state().lcbank2);

    // Each of $C088-$C08F should select bank 1
    for (uint16_t addr = 0xC088; addr <= 0xC08F; addr++)
    {
        f.readSwitch(0xC080);  // Reset to bank 2
        f.readSwitch(addr);
        ASSERT_FALSE(f.state().lcbank2);
    }

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Read Mode (ROM vs RAM)
// ============================================================================
bool test_read_rom_mode()
{
    TEST_CASE("$C081/$C082 set read-from-ROM mode");
    LanguageCardTestFixture f;

    // First enable RAM reading
    f.readSwitch(0xC083);  // Read RAM mode
    ASSERT_TRUE(f.state().lcread);

    // $C081: Read ROM, write enable possible
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcread);

    // Re-enable RAM reading
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);

    // $C082: Read ROM, no write
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);

    TEST_PASS();
    return true;
}

bool test_read_ram_mode()
{
    TEST_CASE("$C080/$C083 set read-from-RAM mode");
    LanguageCardTestFixture f;

    // Start in ROM mode using $C082
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);

    // $C080: Read RAM, no write
    f.readSwitch(0xC080);
    ASSERT_TRUE(f.state().lcread);

    // Back to ROM
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);

    // $C083: Read RAM, write enable possible
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);

    // Verify equivalent addresses work too
    f.readSwitch(0xC082);  // Back to ROM
    f.readSwitch(0xC087);  // Bank 2, read RAM
    ASSERT_TRUE(f.state().lcread);

    f.readSwitch(0xC082);  // Back to ROM
    f.readSwitch(0xC08B);  // Bank 1, read RAM
    ASSERT_TRUE(f.state().lcread);

    f.readSwitch(0xC082);  // Back to ROM
    f.readSwitch(0xC08F);  // Bank 1, read RAM (duplicate)
    ASSERT_TRUE(f.state().lcread);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Write Enable (Double-Read Requirement)
// ============================================================================
bool test_write_enable_double_read()
{
    TEST_CASE("Write enable requires two consecutive reads");
    LanguageCardTestFixture f;

    // Start with write disabled
    f.readSwitch(0xC080);
    ASSERT_FALSE(f.state().lcwrite);
    ASSERT_FALSE(f.state().lcprewrite);

    // First read to $C081 should set prewrite but NOT write
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // Second read to $C081 should enable write
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

bool test_write_enable_c083_double_read()
{
    TEST_CASE("$C083 double-read enables write + RAM read");
    LanguageCardTestFixture f;

    f.reset();

    // First read to $C083
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);       // RAM read enabled immediately
    ASSERT_TRUE(f.state().lcprewrite);   // Pre-write set
    ASSERT_FALSE(f.state().lcwrite);     // Write not yet enabled

    // Second read to $C083
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);       // Still RAM read
    ASSERT_TRUE(f.state().lcwrite);      // Now write is enabled

    TEST_PASS();
    return true;
}

bool test_write_enable_mixed_addresses()
{
    TEST_CASE("Write enable works across different write-enable addresses");
    LanguageCardTestFixture f;

    f.reset();

    // Read $C081 (prewrite)
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // Read $C083 should still enable write (different write-enable address)
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

bool test_prewrite_cleared_by_non_write_enable()
{
    TEST_CASE("Pre-write cleared by non-write-enable addresses");
    LanguageCardTestFixture f;

    f.reset();

    // Set prewrite with $C081
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcprewrite);

    // Read $C080 (no-write address) should clear prewrite
    f.readSwitch(0xC080);
    ASSERT_FALSE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // Now try again - two reads to $C081 should be required
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcwrite);
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Write Disable
// ============================================================================
bool test_write_disable()
{
    TEST_CASE("$C080/$C082 disable write");
    LanguageCardTestFixture f;

    // Enable write first
    f.readSwitch(0xC083);
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);

    // $C080 should disable write
    f.readSwitch(0xC080);
    ASSERT_FALSE(f.state().lcwrite);

    // Re-enable
    f.readSwitch(0xC083);
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);

    // $C082 should also disable write
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Status Read Addresses
// ============================================================================
bool test_status_rdlcbnk2()
{
    TEST_CASE("RDLCBNK2 ($C011) returns bank status");
    LanguageCardTestFixture f;

    // Select bank 2
    f.readSwitch(0xC080);
    uint8_t status = f.mmu.peek(Apple2e::RDLCBNK2);
    ASSERT_EQ(0x80, status);  // Bit 7 set when bank 2 is selected

    // Select bank 1
    f.readSwitch(0xC088);
    status = f.mmu.peek(Apple2e::RDLCBNK2);
    ASSERT_EQ(0x00, status);  // Bit 7 clear when bank 1 is selected

    TEST_PASS();
    return true;
}

bool test_status_rdlcram()
{
    TEST_CASE("RDLCRAM ($C012) returns RAM/ROM read status");
    LanguageCardTestFixture f;

    // Set to read ROM using $C082
    f.readSwitch(0xC082);
    uint8_t status = f.mmu.peek(Apple2e::RDLCRAM);
    ASSERT_EQ(0x00, status);  // Bit 7 clear when reading ROM

    // Set to read RAM
    f.readSwitch(0xC083);
    status = f.mmu.peek(Apple2e::RDLCRAM);
    ASSERT_EQ(0x80, status);  // Bit 7 set when reading RAM

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Memory Access Verification
// ============================================================================
bool test_read_from_rom()
{
    TEST_CASE("Reading $D000-$FFFF returns ROM when lcread=false");
    LanguageCardTestFixture f;

    // Set to read ROM using $C082
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);

    // Read from ROM area - should get ROM pattern (0xEE)
    uint8_t value = f.mmu.read(0xD000);
    ASSERT_EQ(0xEE, value);

    value = f.mmu.read(0xE000);
    ASSERT_EQ(0xEE, value);

    value = f.mmu.read(0xFFFF);
    ASSERT_EQ(0xEE, value);

    TEST_PASS();
    return true;
}

bool test_read_from_ram_bank2()
{
    TEST_CASE("Reading $D000-$FFFF returns RAM bank 2 when lcread=true, lcbank2=true");
    LanguageCardTestFixture f;

    // Set to read RAM, bank 2
    f.readSwitch(0xC083);  // Bank 2, read RAM
    ASSERT_TRUE(f.state().lcread);
    ASSERT_TRUE(f.state().lcbank2);

    // Read from RAM area - should get bank 2 pattern (0xD2)
    uint8_t value = f.mmu.read(0xD000);
    ASSERT_EQ(0xD2, value);

    value = f.mmu.read(0xDFFF);
    ASSERT_EQ(0xD2, value);

    // $E000-$FFFF is shared, should also be 0xD2
    value = f.mmu.read(0xE000);
    ASSERT_EQ(0xD2, value);

    TEST_PASS();
    return true;
}

bool test_read_from_ram_bank1()
{
    TEST_CASE("Reading $D000-$DFFF returns RAM bank 1 when lcread=true, lcbank2=false");
    LanguageCardTestFixture f;

    // Set to read RAM, bank 1
    f.readSwitch(0xC08B);  // Bank 1, read RAM
    ASSERT_TRUE(f.state().lcread);
    ASSERT_FALSE(f.state().lcbank2);

    // Read from $D000-$DFFF - should get bank 1 pattern (0xD1)
    // Bank 1 is stored at $C000-$CFFF in the RAM array
    uint8_t value = f.mmu.read(0xD000);
    ASSERT_EQ(0xD1, value);

    value = f.mmu.read(0xDFFF);
    ASSERT_EQ(0xD1, value);

    // $E000-$FFFF is shared between banks, should still be 0xD2
    value = f.mmu.read(0xE000);
    ASSERT_EQ(0xD2, value);

    TEST_PASS();
    return true;
}

bool test_write_to_ram_bank2()
{
    TEST_CASE("Writing $D000-$FFFF goes to RAM bank 2 when write enabled");
    LanguageCardTestFixture f;

    // Enable RAM write with bank 2
    f.readSwitch(0xC083);
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_TRUE(f.state().lcbank2);

    // Write a test value
    f.mmu.write(0xD000, 0xAA);

    // Verify it was written to bank 2 in RAM
    ASSERT_EQ(0xAA, f.ram.getMainBank()[0xD000]);

    TEST_PASS();
    return true;
}

bool test_write_to_ram_bank1()
{
    TEST_CASE("Writing $D000-$DFFF goes to RAM bank 1 when write enabled");
    LanguageCardTestFixture f;

    // Enable RAM write with bank 1
    f.readSwitch(0xC08B);
    f.readSwitch(0xC08B);
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_FALSE(f.state().lcbank2);

    // Write a test value
    f.mmu.write(0xD000, 0xBB);

    // Verify it was written to bank 1 (stored at $C000 in RAM array)
    ASSERT_EQ(0xBB, f.ram.getMainBank()[0xC000]);  // $D000 - $1000 = $C000

    // Verify bank 2 is unchanged
    ASSERT_EQ(0xD2, f.ram.getMainBank()[0xD000]);

    TEST_PASS();
    return true;
}

bool test_write_disabled_no_effect()
{
    TEST_CASE("Writing when lcwrite=false has no effect");
    LanguageCardTestFixture f;

    // Disable write
    f.readSwitch(0xC080);
    ASSERT_FALSE(f.state().lcwrite);

    // Try to write
    uint8_t original = f.ram.getMainBank()[0xD000];
    f.mmu.write(0xD000, 0x99);

    // Value should be unchanged
    ASSERT_EQ(original, f.ram.getMainBank()[0xD000]);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Shared Bank Region ($E000-$FFFF)
// ============================================================================
bool test_shared_region_bank_independent()
{
    TEST_CASE("$E000-$FFFF is shared between bank 1 and bank 2");
    LanguageCardTestFixture f;

    // Write to shared region with bank 2 selected
    f.readSwitch(0xC083);
    f.readSwitch(0xC083);
    f.mmu.write(0xE000, 0x55);
    f.mmu.write(0xFFFF, 0x66);

    // Switch to bank 1
    f.readSwitch(0xC08B);
    f.readSwitch(0xC08B);

    // Should read the same values (shared region)
    ASSERT_EQ(0x55, f.mmu.read(0xE000));
    ASSERT_EQ(0x66, f.mmu.read(0xFFFF));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: ProDOS-style Sequences
// ============================================================================
bool test_prodos_write_enable_sequence()
{
    TEST_CASE("ProDOS write enable sequence (LDA $C08B, LDA $C08B)");
    LanguageCardTestFixture f;

    f.reset();

    // ProDOS uses this sequence to enable writes to language card
    f.readSwitch(0xC08B);  // First read
    ASSERT_TRUE(f.state().lcread);      // Immediately enables RAM read
    ASSERT_FALSE(f.state().lcwrite);    // Write not yet enabled
    ASSERT_FALSE(f.state().lcbank2);    // Bank 1 selected

    f.readSwitch(0xC08B);  // Second read
    ASSERT_TRUE(f.state().lcwrite);     // Now write is enabled

    // Verify we can write to bank 1
    f.mmu.write(0xD000, 0x77);
    ASSERT_EQ(0x77, f.ram.getMainBank()[0xC000]);  // Bank 1 at offset -$1000

    TEST_PASS();
    return true;
}

bool test_dos33_bank_switch_sequence()
{
    TEST_CASE("DOS 3.3 bank switching sequence");
    LanguageCardTestFixture f;

    f.reset();

    // DOS 3.3 typically uses $C082 to read ROM (for RWTS)
    // $C082 = Read ROM, write disabled
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);
    ASSERT_TRUE(f.state().lcbank2);

    // Verify ROM is visible
    ASSERT_EQ(0xEE, f.mmu.read(0xD000));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: IIe Memory Management Switches
// ============================================================================
bool test_80store_switch()
{
    TEST_CASE("80STORE soft switch ($C000/$C001)");
    LanguageCardTestFixture f;

    // Initially off
    ASSERT_FALSE(f.state().store80);

    // Enable
    f.writeSwitch(Apple2e::SET80STORE);
    ASSERT_TRUE(f.state().store80);

    // Disable
    f.writeSwitch(Apple2e::CLR80STORE);
    ASSERT_FALSE(f.state().store80);

    TEST_PASS();
    return true;
}

bool test_ramrd_switch()
{
    TEST_CASE("RAMRD soft switch ($C002/$C003)");
    LanguageCardTestFixture f;

    // Initially main
    ASSERT_FALSE(f.state().ramrd);

    // Switch to aux
    f.writeSwitch(Apple2e::RDCARDRAM);
    ASSERT_TRUE(f.state().ramrd);

    // Switch to main
    f.writeSwitch(Apple2e::RDMAINRAM);
    ASSERT_FALSE(f.state().ramrd);

    TEST_PASS();
    return true;
}

bool test_ramwrt_switch()
{
    TEST_CASE("RAMWRT soft switch ($C004/$C005)");
    LanguageCardTestFixture f;

    // Initially main
    ASSERT_FALSE(f.state().ramwrt);

    // Switch to aux
    f.writeSwitch(Apple2e::WRCARDRAM);
    ASSERT_TRUE(f.state().ramwrt);

    // Switch to main
    f.writeSwitch(Apple2e::WRMAINRAM);
    ASSERT_FALSE(f.state().ramwrt);

    TEST_PASS();
    return true;
}

bool test_altzp_switch()
{
    TEST_CASE("ALTZP soft switch ($C008/$C009)");
    LanguageCardTestFixture f;

    // Initially main
    ASSERT_FALSE(f.state().altzp);

    // Switch to aux
    f.writeSwitch(Apple2e::SETALTZP);
    ASSERT_TRUE(f.state().altzp);

    // Switch to main
    f.writeSwitch(Apple2e::SETSTDZP);
    ASSERT_FALSE(f.state().altzp);

    TEST_PASS();
    return true;
}

bool test_intcxrom_switch()
{
    TEST_CASE("INTCXROM soft switch ($C006/$C007)");
    LanguageCardTestFixture f;

    // Initially slot ROM
    ASSERT_FALSE(f.state().intcxrom);

    // Switch to internal
    f.writeSwitch(Apple2e::SETINTCXROM);
    ASSERT_TRUE(f.state().intcxrom);

    // Switch to slot
    f.writeSwitch(Apple2e::SETSLOTCXROM);
    ASSERT_FALSE(f.state().intcxrom);

    TEST_PASS();
    return true;
}

bool test_slotc3rom_switch()
{
    TEST_CASE("SLOTC3ROM soft switch ($C00A/$C00B)");
    LanguageCardTestFixture f;

    // Initially internal
    ASSERT_FALSE(f.state().slotc3rom);

    // Switch to slot
    f.writeSwitch(Apple2e::SETSLOTC3ROM);
    ASSERT_TRUE(f.state().slotc3rom);

    // Switch to internal
    f.writeSwitch(Apple2e::SETINTC3ROM);
    ASSERT_FALSE(f.state().slotc3rom);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: ALTZP affects Language Card
// ============================================================================
bool test_altzp_affects_language_card()
{
    TEST_CASE("ALTZP routes language card to aux memory");
    LanguageCardTestFixture f;

    // Initialize aux RAM language card area with different pattern
    auto& auxRam = f.ram.getAuxBank();
    for (uint32_t addr = 0xD000; addr <= 0xFFFF; addr++)
    {
        auxRam[addr] = 0xAA;  // Aux pattern
    }

    // Enable RAM read with bank 2
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);

    // Without ALTZP, should read main RAM (0xD2)
    ASSERT_EQ(0xD2, f.mmu.read(0xD000));

    // Enable ALTZP
    f.writeSwitch(Apple2e::SETALTZP);
    ASSERT_TRUE(f.state().altzp);

    // Now should read aux RAM (0xAA)
    ASSERT_EQ(0xAA, f.mmu.read(0xD000));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Zero Page Bank Switching
// ============================================================================
bool test_zero_page_main()
{
    TEST_CASE("Zero page reads/writes go to main RAM when ALTZP=false");
    LanguageCardTestFixture f;

    // Initialize different patterns in main and aux zero page
    f.ram.getMainBank()[0x00] = 0x11;
    f.ram.getAuxBank()[0x00] = 0x22;

    // ALTZP off (default)
    ASSERT_FALSE(f.state().altzp);

    // Should read from main
    ASSERT_EQ(0x11, f.mmu.read(0x00));

    // Write should go to main
    f.mmu.write(0x00, 0x33);
    ASSERT_EQ(0x33, f.ram.getMainBank()[0x00]);
    ASSERT_EQ(0x22, f.ram.getAuxBank()[0x00]);  // Aux unchanged

    TEST_PASS();
    return true;
}

bool test_zero_page_aux()
{
    TEST_CASE("Zero page reads/writes go to aux RAM when ALTZP=true");
    LanguageCardTestFixture f;

    // Initialize different patterns
    f.ram.getMainBank()[0x00] = 0x11;
    f.ram.getAuxBank()[0x00] = 0x22;

    // Enable ALTZP
    f.writeSwitch(Apple2e::SETALTZP);
    ASSERT_TRUE(f.state().altzp);

    // Should read from aux
    ASSERT_EQ(0x22, f.mmu.read(0x00));

    // Write should go to aux
    f.mmu.write(0x00, 0x44);
    ASSERT_EQ(0x11, f.ram.getMainBank()[0x00]);  // Main unchanged
    ASSERT_EQ(0x44, f.ram.getAuxBank()[0x00]);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Stack Bank Switching
// ============================================================================
bool test_stack_main()
{
    TEST_CASE("Stack reads/writes go to main RAM when ALTZP=false");
    LanguageCardTestFixture f;

    // Initialize different patterns
    f.ram.getMainBank()[0x1FF] = 0x55;
    f.ram.getAuxBank()[0x1FF] = 0x66;

    ASSERT_FALSE(f.state().altzp);

    // Should read from main
    ASSERT_EQ(0x55, f.mmu.read(0x1FF));

    TEST_PASS();
    return true;
}

bool test_stack_aux()
{
    TEST_CASE("Stack reads/writes go to aux RAM when ALTZP=true");
    LanguageCardTestFixture f;

    // Initialize different patterns
    f.ram.getMainBank()[0x1FF] = 0x55;
    f.ram.getAuxBank()[0x1FF] = 0x66;

    // Enable ALTZP
    f.writeSwitch(Apple2e::SETALTZP);
    ASSERT_TRUE(f.state().altzp);

    // Should read from aux
    ASSERT_EQ(0x66, f.mmu.read(0x1FF));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: 80STORE and PAGE2 interaction
// ============================================================================
bool test_80store_text_page_switching()
{
    TEST_CASE("80STORE causes PAGE2 to switch text page between main/aux");
    LanguageCardTestFixture f;

    // Initialize text page 1 in main and aux with different patterns
    f.ram.getMainBank()[0x400] = 0xAA;
    f.ram.getAuxBank()[0x400] = 0xBB;

    // Enable 80STORE
    f.writeSwitch(Apple2e::SET80STORE);
    ASSERT_TRUE(f.state().store80);

    // PAGE1 selected - should read main
    f.readSwitch(Apple2e::TXTPAGE1);
    ASSERT_EQ(0xAA, f.mmu.read(0x400));

    // PAGE2 selected - should read aux
    f.readSwitch(Apple2e::TXTPAGE2);
    ASSERT_EQ(0xBB, f.mmu.read(0x400));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Video Soft Switches
// ============================================================================
bool test_video_text_graphics_switch()
{
    TEST_CASE("TEXT/GRAPHICS mode switch ($C050/$C051)");
    LanguageCardTestFixture f;

    // Initially text mode
    ASSERT_EQ(Apple2e::VideoMode::TEXT, f.state().video_mode);

    // Switch to graphics
    f.readSwitch(Apple2e::TXTCLR);
    ASSERT_EQ(Apple2e::VideoMode::GRAPHICS, f.state().video_mode);

    // Switch to text
    f.readSwitch(Apple2e::TXTSET);
    ASSERT_EQ(Apple2e::VideoMode::TEXT, f.state().video_mode);

    TEST_PASS();
    return true;
}

bool test_video_mixed_full_switch()
{
    TEST_CASE("MIXED/FULL mode switch ($C052/$C053)");
    LanguageCardTestFixture f;

    // Initially full
    ASSERT_EQ(Apple2e::ScreenMode::FULL, f.state().screen_mode);

    // Switch to mixed
    f.readSwitch(Apple2e::MIXSET);
    ASSERT_EQ(Apple2e::ScreenMode::MIXED, f.state().screen_mode);

    // Switch to full
    f.readSwitch(Apple2e::MIXCLR);
    ASSERT_EQ(Apple2e::ScreenMode::FULL, f.state().screen_mode);

    TEST_PASS();
    return true;
}

bool test_video_page_switch()
{
    TEST_CASE("PAGE1/PAGE2 switch ($C054/$C055)");
    LanguageCardTestFixture f;

    // Initially page 1
    ASSERT_EQ(Apple2e::PageSelect::PAGE1, f.state().page_select);

    // Switch to page 2
    f.readSwitch(Apple2e::TXTPAGE2);
    ASSERT_EQ(Apple2e::PageSelect::PAGE2, f.state().page_select);

    // Switch to page 1
    f.readSwitch(Apple2e::TXTPAGE1);
    ASSERT_EQ(Apple2e::PageSelect::PAGE1, f.state().page_select);

    TEST_PASS();
    return true;
}

bool test_video_lores_hires_switch()
{
    TEST_CASE("LORES/HIRES switch ($C056/$C057)");
    LanguageCardTestFixture f;

    // Initially lores
    ASSERT_EQ(Apple2e::GraphicsMode::LORES, f.state().graphics_mode);

    // Switch to hires
    f.readSwitch(Apple2e::HIRES);
    ASSERT_EQ(Apple2e::GraphicsMode::HIRES, f.state().graphics_mode);

    // Switch to lores
    f.readSwitch(Apple2e::LORES);
    ASSERT_EQ(Apple2e::GraphicsMode::LORES, f.state().graphics_mode);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: 80-Column Mode
// ============================================================================
bool test_80col_switch()
{
    TEST_CASE("80COL mode switch ($C00C/$C00D)");
    LanguageCardTestFixture f;

    // Initially 40-column
    ASSERT_FALSE(f.state().col80_mode);

    // Switch to 80-column
    f.writeSwitch(Apple2e::SET80VID);
    ASSERT_TRUE(f.state().col80_mode);

    // Switch to 40-column
    f.writeSwitch(Apple2e::CLR80VID);
    ASSERT_FALSE(f.state().col80_mode);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Alternate Character Set
// ============================================================================
bool test_altchar_switch()
{
    TEST_CASE("ALTCHAR mode switch ($C00E/$C00F)");
    LanguageCardTestFixture f;

    // Initially primary
    ASSERT_FALSE(f.state().altchar_mode);

    // Switch to alternate
    f.writeSwitch(Apple2e::SETALTCHAR);
    ASSERT_TRUE(f.state().altchar_mode);

    // Switch to primary
    f.writeSwitch(Apple2e::CLRALTCHAR);
    ASSERT_FALSE(f.state().altchar_mode);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Status Register Reads
// ============================================================================
bool test_status_registers()
{
    TEST_CASE("All status registers return correct bit 7 values");
    LanguageCardTestFixture f;

    // Set various states
    f.writeSwitch(Apple2e::SET80STORE);
    f.writeSwitch(Apple2e::RDCARDRAM);
    f.writeSwitch(Apple2e::WRCARDRAM);
    f.writeSwitch(Apple2e::SETALTZP);
    f.writeSwitch(Apple2e::SETINTCXROM);
    f.writeSwitch(Apple2e::SETSLOTC3ROM);

    // Check status registers
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RD80STORE));
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RDRAMRD));
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RDRAMWRT));
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RDALTZP));
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RDCXROM));
    ASSERT_EQ(0x80, f.mmu.peek(Apple2e::RDC3ROM));

    // Clear states
    f.writeSwitch(Apple2e::CLR80STORE);
    f.writeSwitch(Apple2e::RDMAINRAM);
    f.writeSwitch(Apple2e::WRMAINRAM);
    f.writeSwitch(Apple2e::SETSTDZP);
    f.writeSwitch(Apple2e::SETSLOTCXROM);
    f.writeSwitch(Apple2e::SETINTC3ROM);

    // Check status registers again
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RD80STORE));
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RDRAMRD));
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RDRAMWRT));
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RDALTZP));
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RDCXROM));
    ASSERT_EQ(0x00, f.mmu.peek(Apple2e::RDC3ROM));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Write-Enable Persistence Across Bank Switches
// Per technical documentation: write-enable state persists when switching banks
// Reference: comp.sys.apple2 discussion - "ProDOS relies on this behaviour"
// ============================================================================
bool test_write_enable_persists_across_bank_switch()
{
    TEST_CASE("Write-enable persists when switching from bank 2 to bank 1");
    LanguageCardTestFixture f;

    f.reset();

    // Enable write with bank 2 using $C083
    f.readSwitch(0xC083);
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_TRUE(f.state().lcbank2);

    // Switch to bank 1 with $C08B - write should REMAIN enabled
    // This is critical for ProDOS behavior
    f.readSwitch(0xC08B);
    ASSERT_FALSE(f.state().lcbank2);  // Bank 1 now
    ASSERT_TRUE(f.state().lcwrite);   // Write still enabled!

    TEST_PASS();
    return true;
}

bool test_write_enable_persists_bank1_to_bank2()
{
    TEST_CASE("Write-enable persists when switching from bank 1 to bank 2");
    LanguageCardTestFixture f;

    f.reset();

    // Enable write with bank 1 using $C08B
    f.readSwitch(0xC08B);
    f.readSwitch(0xC08B);
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_FALSE(f.state().lcbank2);

    // Switch to bank 2 with $C083 - write should REMAIN enabled
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcbank2);   // Bank 2 now
    ASSERT_TRUE(f.state().lcwrite);   // Write still enabled!

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Write Access Resets Prewrite Counter
// Per technical documentation: "Any in-between write will reset the counter
// and require two more READS"
// Reference: MemoryMap.IIe.64K.128K.txt
// ============================================================================
bool test_write_resets_prewrite_counter()
{
    TEST_CASE("Write to LC switch resets prewrite counter");
    LanguageCardTestFixture f;

    f.reset();

    // First read to $C081 sets prewrite
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // Write to $C081 should reset prewrite (not enable write)
    f.writeSwitch(0xC081);
    ASSERT_FALSE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // Now need two more reads to enable write
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcwrite);
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

bool test_write_does_not_enable_lcwrite()
{
    TEST_CASE("Write to LC switch never enables lcwrite");
    LanguageCardTestFixture f;

    f.reset();

    // Set prewrite with a read
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcprewrite);

    // Write should NOT enable lcwrite even with prewrite set
    f.writeSwitch(0xC083);
    ASSERT_FALSE(f.state().lcwrite);

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: $C080 Enables RAM Read (Critical Hardware Behavior)
// Per technical documentation: "$C080: RD LC RAM bank2, WR-protect LC RAM"
// Reference: kreativekorp.com, MemoryMap.IIe.64K.128K.txt
// ============================================================================
bool test_c080_enables_ram_read()
{
    TEST_CASE("$C080 enables reading from RAM (not ROM)");
    LanguageCardTestFixture f;

    // Start with ROM visible
    f.reset();
    ASSERT_FALSE(f.state().lcread);  // Reading ROM initially

    // $C080 should enable RAM read for bank 2
    f.readSwitch(0xC080);
    
    // Per hardware docs: $C080 = "RD LC RAM bank2, WR-protect LC RAM"
    // lcread should be TRUE (read from RAM, not ROM)
    ASSERT_TRUE(f.state().lcread);
    ASSERT_TRUE(f.state().lcbank2);
    ASSERT_FALSE(f.state().lcwrite);

    // Verify we can read RAM pattern, not ROM pattern
    ASSERT_EQ(0xD2, f.mmu.read(0xD000));  // RAM bank 2 pattern

    TEST_PASS();
    return true;
}

bool test_c088_enables_ram_read_bank1()
{
    TEST_CASE("$C088 enables reading from RAM bank 1");
    LanguageCardTestFixture f;

    f.reset();

    // $C088 should enable RAM read for bank 1
    f.readSwitch(0xC088);
    
    // Per hardware docs: $C088 = "RD LC RAM bank1, WR-protect LC RAM"
    ASSERT_TRUE(f.state().lcread);
    ASSERT_FALSE(f.state().lcbank2);  // Bank 1
    ASSERT_FALSE(f.state().lcwrite);

    // Verify we can read RAM bank 1 pattern
    ASSERT_EQ(0xD1, f.mmu.read(0xD000));

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: ROM/RAM Read Patterns Match Hardware Documentation
// Reference: kreativekorp.com Apple II I/O Memory documentation
// $C080: Read RAM bank 2; no write
// $C081: Read ROM; write RAM bank 2 (after double read)
// $C082: Read ROM; no write
// $C083: Read/write RAM bank 2 (after double read for write)
// ============================================================================
bool test_lc_switch_read_patterns()
{
    TEST_CASE("LC switch patterns match hardware documentation");
    LanguageCardTestFixture f;

    // $C080: Read RAM, no write
    f.reset();
    f.readSwitch(0xC080);
    ASSERT_TRUE(f.state().lcread);   // Read RAM
    ASSERT_FALSE(f.state().lcwrite); // No write
    ASSERT_TRUE(f.state().lcbank2);  // Bank 2

    // $C081: Read ROM, write enable possible
    f.reset();
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcread);  // Read ROM
    ASSERT_TRUE(f.state().lcprewrite);
    f.readSwitch(0xC081);
    ASSERT_TRUE(f.state().lcwrite);  // Write enabled after 2 reads

    // $C082: Read ROM, no write
    f.reset();
    f.readSwitch(0xC082);
    ASSERT_FALSE(f.state().lcread);  // Read ROM
    ASSERT_FALSE(f.state().lcwrite); // No write
    ASSERT_FALSE(f.state().lcprewrite);

    // $C083: Read RAM, write enable possible
    f.reset();
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);   // Read RAM
    ASSERT_TRUE(f.state().lcprewrite);
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcwrite);  // Write enabled after 2 reads

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Sequence from Technical Documentation
// "LDA $C081, LDA $C081, LDA $C083" results in LC RAM being writeable
// Reference: comp.sys.apple2 discussion
// ============================================================================
bool test_rom_to_ram_with_write_sequence()
{
    TEST_CASE("$C081,$C081,$C083 enables RAM write (switch ROM to RAM)");
    LanguageCardTestFixture f;

    f.reset();

    // LDA $C081 - first read, sets prewrite, reads ROM
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcread);
    ASSERT_TRUE(f.state().lcprewrite);
    ASSERT_FALSE(f.state().lcwrite);

    // LDA $C081 - second read, enables write, still reads ROM
    f.readSwitch(0xC081);
    ASSERT_FALSE(f.state().lcread);
    ASSERT_TRUE(f.state().lcwrite);

    // LDA $C083 - switch to RAM, write should REMAIN enabled
    f.readSwitch(0xC083);
    ASSERT_TRUE(f.state().lcread);   // Now reads RAM
    ASSERT_TRUE(f.state().lcwrite);  // Write still enabled!

    TEST_PASS();
    return true;
}

// ============================================================================
// Test: Edge Cases
// ============================================================================
bool test_duplicate_switch_addresses()
{
    TEST_CASE("Duplicate switch addresses ($C084-$C087, $C08C-$C08F) work correctly");
    LanguageCardTestFixture f;

    // $C084 should behave like $C080: Read RAM, no write
    f.reset();
    f.readSwitch(0xC084);
    ASSERT_TRUE(f.state().lcread);   // Read RAM
    ASSERT_FALSE(f.state().lcwrite);
    ASSERT_TRUE(f.state().lcbank2);

    // $C085 should behave like $C081: Read ROM, write enable after 2 reads
    f.reset();
    f.readSwitch(0xC085);
    f.readSwitch(0xC085);
    ASSERT_FALSE(f.state().lcread);  // Read ROM
    ASSERT_TRUE(f.state().lcwrite);

    // $C086 should behave like $C082: Read ROM, no write
    f.reset();
    f.readSwitch(0xC086);
    ASSERT_FALSE(f.state().lcread);  // Read ROM
    ASSERT_FALSE(f.state().lcwrite);

    // $C087 should behave like $C083: Read RAM, write enable after 2 reads
    f.reset();
    f.readSwitch(0xC087);
    f.readSwitch(0xC087);
    ASSERT_TRUE(f.state().lcread);   // Read RAM
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_TRUE(f.state().lcbank2);

    // $C08C-$C08F for bank 1
    f.reset();
    f.readSwitch(0xC08F);
    f.readSwitch(0xC08F);
    ASSERT_TRUE(f.state().lcread);   // Read RAM
    ASSERT_TRUE(f.state().lcwrite);
    ASSERT_FALSE(f.state().lcbank2);

    TEST_PASS();
    return true;
}

bool test_rapid_bank_switching()
{
    TEST_CASE("Rapid bank switching maintains correct state");
    LanguageCardTestFixture f;

    // Rapidly switch between banks and modes
    for (int i = 0; i < 100; i++)
    {
        f.readSwitch(0xC083);  // Bank 2, RAM, prewrite
        f.readSwitch(0xC083);  // Enable write
        ASSERT_TRUE(f.state().lcread);
        ASSERT_TRUE(f.state().lcwrite);
        ASSERT_TRUE(f.state().lcbank2);

        f.readSwitch(0xC08B);  // Bank 1, RAM
        f.readSwitch(0xC08B);  // Enable write
        ASSERT_TRUE(f.state().lcread);
        ASSERT_TRUE(f.state().lcwrite);
        ASSERT_FALSE(f.state().lcbank2);

        f.readSwitch(0xC082);  // Bank 2, ROM, no write
        ASSERT_FALSE(f.state().lcread);
        ASSERT_FALSE(f.state().lcwrite);
        ASSERT_TRUE(f.state().lcbank2);
    }

    TEST_PASS();
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main()
{
    std::cout << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "  Language Card and Bank Switching Softswitch Tests" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;

    std::vector<std::function<bool()>> tests = {
        // Initial state
        test_initial_state,

        // Bank selection
        test_bank_selection_bank2,
        test_bank_selection_bank1,

        // Read mode
        test_read_rom_mode,
        test_read_ram_mode,

        // Write enable
        test_write_enable_double_read,
        test_write_enable_c083_double_read,
        test_write_enable_mixed_addresses,
        test_prewrite_cleared_by_non_write_enable,

        // Write disable
        test_write_disable,

        // Status reads
        test_status_rdlcbnk2,
        test_status_rdlcram,

        // Memory access
        test_read_from_rom,
        test_read_from_ram_bank2,
        test_read_from_ram_bank1,
        test_write_to_ram_bank2,
        test_write_to_ram_bank1,
        test_write_disabled_no_effect,

        // Shared region
        test_shared_region_bank_independent,

        // Real-world sequences
        test_prodos_write_enable_sequence,
        test_dos33_bank_switch_sequence,

        // IIe memory management
        test_80store_switch,
        test_ramrd_switch,
        test_ramwrt_switch,
        test_altzp_switch,
        test_intcxrom_switch,
        test_slotc3rom_switch,

        // ALTZP effects
        test_altzp_affects_language_card,

        // Zero page and stack
        test_zero_page_main,
        test_zero_page_aux,
        test_stack_main,
        test_stack_aux,

        // 80STORE and PAGE2
        test_80store_text_page_switching,

        // Video switches
        test_video_text_graphics_switch,
        test_video_mixed_full_switch,
        test_video_page_switch,
        test_video_lores_hires_switch,

        // 80-column and alt char
        test_80col_switch,
        test_altchar_switch,

        // Status registers
        test_status_registers,

        // Write-enable persistence (ProDOS compatibility)
        test_write_enable_persists_across_bank_switch,
        test_write_enable_persists_bank1_to_bank2,

        // Write access behavior
        test_write_resets_prewrite_counter,
        test_write_does_not_enable_lcwrite,

        // $C080/$C088 RAM read behavior (critical hardware accuracy)
        test_c080_enables_ram_read,
        test_c088_enables_ram_read_bank1,

        // Hardware documentation compliance
        test_lc_switch_read_patterns,
        test_rom_to_ram_with_write_sequence,

        // Edge cases
        test_duplicate_switch_addresses,
        test_rapid_bank_switching,
    };

    std::cout << "Running " << tests.size() << " tests..." << std::endl;
    std::cout << std::endl;

    for (auto& test : tests)
    {
        test();
    }

    std::cout << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
