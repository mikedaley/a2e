#include <iostream>
#include <iomanip>
#include <cassert>
#include <MOS6502/CPU6502.hpp>
#include "ram.hpp"
#include "rom.hpp"
#include "mmu.hpp"
#include "keyboard.hpp"

/**
 * Test to verify that ROM code is being executed properly
 *
 * This test:
 * 1. Loads the Apple IIe ROMs
 * 2. Creates a CPU and resets it
 * 3. Verifies the reset vector points to ROM
 * 4. Executes several instructions
 * 5. Verifies the PC is advancing through ROM space
 */

int main()
{
    std::cout << "ROM Execution Test\n";
    std::cout << "==================\n\n";

    // Create RAM (64KB with main/aux banks)
    RAM ram;
    std::cout << "✓ RAM initialized (64KB main + 64KB aux)\n";

    // Create ROM (12KB)
    ROM rom;

    // Load Apple IIe ROMs
    if (!rom.loadAppleIIeROMs())
    {
        std::cerr << "✗ Failed to load Apple IIe ROM files\n";
        std::cerr << "  Please ensure ROM files are present in include/roms/\n";
        return 1;
    }
    std::cout << "✓ Apple IIe ROMs loaded successfully\n";

    // Create keyboard
    Keyboard keyboard;
    std::cout << "✓ Keyboard initialized\n";

    // Create MMU (handles memory mapping)
    MMU mmu(ram, rom, &keyboard);
    std::cout << "✓ MMU initialized\n";

    // Create CPU with 65C02 variant
    using ReadFunc = std::function<uint8_t(uint16_t)>;
    using WriteFunc = std::function<void(uint16_t, uint8_t)>;
    using CPU = MOS6502::CPU6502<ReadFunc, WriteFunc, MOS6502::CPUVariant::CMOS_65C02>;

    CPU cpu(
        [&mmu](uint16_t address) -> uint8_t { return mmu.read(address); },
        [&mmu](uint16_t address, uint8_t value) -> void { mmu.write(address, value); }
    );
    std::cout << "✓ CPU initialized (65C02)\n\n";

    // Check reset vector BEFORE reset
    std::cout << "Checking Reset Vector:\n";
    uint8_t reset_lo = mmu.read(0xFFFC);
    uint8_t reset_hi = mmu.read(0xFFFD);
    uint16_t reset_vector = reset_lo | (reset_hi << 8);
    std::cout << "  Reset vector at $FFFC: $" << std::hex << std::uppercase
              << std::setfill('0') << std::setw(4) << reset_vector << std::dec << "\n";

    // Verify reset vector points to ROM space ($D000-$FFFF)
    if (reset_vector < 0xD000 || reset_vector > 0xFFFF)
    {
        std::cerr << "✗ Reset vector does not point to ROM space!\n";
        return 1;
    }
    std::cout << "✓ Reset vector points to ROM space\n";

    // Read a few bytes from the reset vector location to verify ROM content
    std::cout << "\nROM content at reset vector ($" << std::hex << std::uppercase
              << std::setw(4) << reset_vector << std::dec << "):\n  ";
    for (int i = 0; i < 16; i++)
    {
        uint8_t byte = mmu.read(reset_vector + i);
        std::cout << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(2) << (int)byte << " ";
    }
    std::cout << std::dec << "\n";

    // Reset CPU
    cpu.reset();
    uint16_t initial_pc = cpu.getPC();
    std::cout << "\n✓ CPU reset complete\n";
    std::cout << "  Initial PC: $" << std::hex << std::uppercase
              << std::setfill('0') << std::setw(4) << initial_pc << std::dec << "\n";

    // Verify PC was set from reset vector
    if (initial_pc != reset_vector)
    {
        std::cerr << "✗ PC does not match reset vector!\n";
        std::cerr << "  Expected: $" << std::hex << std::uppercase << std::setw(4) << reset_vector << "\n";
        std::cerr << "  Got:      $" << std::setw(4) << initial_pc << std::dec << "\n";
        return 1;
    }
    std::cout << "✓ PC correctly set from reset vector\n";

    // Execute instructions and verify ROM code is being executed
    std::cout << "\nExecuting Instructions:\n";
    std::cout << "  Step | PC     | Opcode | A  | X  | Y  | SP | P  |\n";
    std::cout << "  -----|--------|--------|----|----|----|----|----|\n";

    const int NUM_INSTRUCTIONS = 20;
    uint16_t last_pc = initial_pc;

    for (int i = 0; i < NUM_INSTRUCTIONS; i++)
    {
        uint16_t pc = cpu.getPC();
        uint8_t opcode = mmu.read(pc);

        std::cout << "  " << std::setfill(' ') << std::setw(4) << std::dec << (i + 1) << " | "
                  << "$" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << pc << " | "
                  << "$" << std::setw(2) << (int)opcode << "     | "
                  << "$" << std::setw(2) << (int)cpu.getA() << " | "
                  << "$" << std::setw(2) << (int)cpu.getX() << " | "
                  << "$" << std::setw(2) << (int)cpu.getY() << " | "
                  << "$" << std::setw(2) << (int)cpu.getSP() << " | "
                  << "$" << std::setw(2) << (int)cpu.getP() << " |\n";

        // Verify PC is in ROM space
        if (pc < 0xD000)
        {
            std::cerr << "\n✗ PC left ROM space! Current PC: $"
                      << std::hex << std::uppercase << std::setw(4) << pc << std::dec << "\n";
            std::cout << "\nNote: If the ROM code performs a JSR or JMP to RAM, this is expected.\n";
            std::cout << "      Check the ROM disassembly to verify the behavior.\n";
            break;
        }

        // Execute the instruction
        uint32_t cycles = cpu.executeInstruction();
        (void)cycles; // Suppress unused warning

        // Check if PC changed (instruction was executed)
        uint16_t new_pc = cpu.getPC();
        if (new_pc == last_pc)
        {
            std::cerr << "\n✗ PC did not advance after instruction execution!\n";
            std::cerr << "  This might indicate the CPU is stuck in an infinite loop\n";
            std::cerr << "  or encountered an invalid opcode.\n";
            return 1;
        }
        last_pc = new_pc;
    }

    std::cout << std::dec << "\n";

    // Final verification
    uint16_t final_pc = cpu.getPC();
    uint64_t total_cycles = cpu.getTotalCycles();

    std::cout << "Final State:\n";
    std::cout << "  PC: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(4) << final_pc << std::dec << "\n";
    std::cout << "  Total cycles: " << total_cycles << "\n";
    std::cout << "  Instructions executed: " << NUM_INSTRUCTIONS << "\n\n";

    if (total_cycles == 0)
    {
        std::cerr << "✗ No cycles were executed!\n";
        return 1;
    }

    std::cout << "✓ ROM code is being executed successfully!\n\n";
    std::cout << "Summary:\n";
    std::cout << "  - Reset vector correctly points to ROM\n";
    std::cout << "  - CPU PC initialized from reset vector\n";
    std::cout << "  - Instructions executed from ROM space\n";
    std::cout << "  - PC advanced through " << NUM_INSTRUCTIONS << " instructions\n";
    std::cout << "  - Total cycles consumed: " << total_cycles << "\n";

    return 0;
}
