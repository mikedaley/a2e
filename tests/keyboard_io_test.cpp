#include <iostream>
#include <iomanip>
#include <cassert>
#include "emulator/keyboard.hpp"
#include "apple2e/soft_switches.hpp"

/**
 * Test to verify keyboard I/O returns proper "no key press" values
 *
 * This test verifies:
 * 1. Reading $C000 returns 0x00 (bit 7 clear) when no key is pressed
 * 2. Reading $C010 returns 0x00 when strobe is cleared
 * 3. Reading $C000 returns key | 0x80 when key is pressed
 * 4. Reading $C010 clears the strobe and removes key from queue
 */

int main()
{
    std::cout << "Keyboard I/O Test\n";
    std::cout << "=================\n\n";

    Keyboard keyboard;
    std::cout << "✓ Keyboard initialized\n\n";

    // Test 1: Reading KB_DATA with no key pressed
    std::cout << "Test 1: Reading $C000 (KB_DATA) with no key pressed\n";
    uint8_t result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x00)
    {
        std::cerr << "✗ FAILED: Expected $00, got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $00 (bit 7 clear = no key)\n\n";

    // Test 2: Reading KB_STROBE with no key pressed
    std::cout << "Test 2: Reading $C010 (KB_STROBE) with no key pressed\n";
    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x00)
    {
        std::cerr << "✗ FAILED: Expected $00, got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $00 (strobe cleared)\n\n";

    // Test 3: Press a key and read KB_DATA
    std::cout << "Test 3: Press 'A' (key code $41) and read $C000\n";
    keyboard.keyDown(0x41); // 'A' key
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0xC1) // 0x41 | 0x80 = 0xC1
    {
        std::cerr << "✗ FAILED: Expected $C1 (0x41 | 0x80), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $C1 (bit 7 set = key available)\n";
    std::cout << "  Key code: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)(result & 0x7F) << std::dec << "\n\n";

    // Test 4: Read KB_DATA again (should still return same key)
    std::cout << "Test 4: Read $C000 again (key still in queue)\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0xC1)
    {
        std::cerr << "✗ FAILED: Expected $C1, got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Key remains in queue until strobe cleared\n\n";

    // Test 5: Clear the strobe by reading KB_STROBE
    std::cout << "Test 5: Read $C010 to clear strobe\n";
    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x80)
    {
        std::cerr << "✗ FAILED: Expected $80 (strobe was set), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $80 (strobe was set)\n";
    std::cout << "  Strobe cleared and key removed from queue\n\n";

    // Test 6: Read KB_DATA after strobe cleared (should be no key)
    std::cout << "Test 6: Read $C000 after clearing strobe\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x00)
    {
        std::cerr << "✗ FAILED: Expected $00 (no key), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $00 (no key pressed)\n\n";

    // Test 7: Read KB_STROBE again (should be cleared)
    std::cout << "Test 7: Read $C010 again (strobe should stay cleared)\n";
    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "  Result: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x00)
    {
        std::cerr << "✗ FAILED: Expected $00 (strobe cleared), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $00 (strobe remains cleared)\n\n";

    // Test 8: Multiple key presses (queue behavior)
    std::cout << "Test 8: Press multiple keys (B, C, D) and verify queue\n";
    keyboard.keyDown(0x42); // 'B'
    keyboard.keyDown(0x43); // 'C'
    keyboard.keyDown(0x44); // 'D'

    std::cout << "  Reading first key:\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "    KB_DATA: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << " (key: $" << std::setw(2)
              << (int)(result & 0x7F) << ")" << std::dec << "\n";
    if ((result & 0x7F) != 0x42)
    {
        std::cerr << "✗ FAILED: Expected 'B' ($42), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)(result & 0x7F) << std::dec << "\n";
        return 1;
    }

    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "    KB_STROBE: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << " (cleared)\n";

    std::cout << "  Reading second key:\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "    KB_DATA: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << " (key: $" << std::setw(2)
              << (int)(result & 0x7F) << ")" << std::dec << "\n";
    if ((result & 0x7F) != 0x43)
    {
        std::cerr << "✗ FAILED: Expected 'C' ($43), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)(result & 0x7F) << std::dec << "\n";
        return 1;
    }

    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "    KB_STROBE: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << " (cleared)\n";

    std::cout << "  Reading third key:\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "    KB_DATA: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << " (key: $" << std::setw(2)
              << (int)(result & 0x7F) << ")" << std::dec << "\n";
    if ((result & 0x7F) != 0x44)
    {
        std::cerr << "✗ FAILED: Expected 'D' ($44), got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)(result & 0x7F) << std::dec << "\n";
        return 1;
    }

    result = keyboard.read(Apple2e::KB_STROBE);
    std::cout << "    KB_STROBE: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << " (cleared)\n";

    std::cout << "✓ PASSED: Queue operates correctly (FIFO)\n\n";

    // Final verification: No key pressed
    std::cout << "Test 9: Final verification - no key pressed\n";
    result = keyboard.read(Apple2e::KB_DATA);
    std::cout << "  KB_DATA: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)result << std::dec << "\n";

    if (result != 0x00)
    {
        std::cerr << "✗ FAILED: Expected $00, got $" << std::hex << std::uppercase
                  << std::setw(2) << (int)result << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PASSED: Returns $00 (no key pressed)\n\n";

    std::cout << "================================================\n";
    std::cout << "All tests passed!\n\n";
    std::cout << "Summary:\n";
    std::cout << "  - $C000 returns $00 when no key pressed (bit 7 clear)\n";
    std::cout << "  - $C000 returns key|$80 when key pressed (bit 7 set)\n";
    std::cout << "  - $C010 returns $00 when strobe already cleared\n";
    std::cout << "  - $C010 returns $80 when clearing strobe\n";
    std::cout << "  - Reading $C010 clears strobe and removes key from queue\n";
    std::cout << "  - Multiple keys are queued in FIFO order\n";

    return 0;
}
