#include <iostream>
#include <iomanip>
#include <MOS6502/CPU6502.hpp>
#include "emulator/ram.hpp"
#include "emulator/rom.hpp"
#include "emulator/mmu.hpp"
#include "emulator/keyboard.hpp"

/**
 * Diagnostic test to investigate why PC gets stuck at $D01B
 */

int main()
{
    std::cout << "PC Stuck Diagnostic Test\n";
    std::cout << "=========================\n\n";

    // Initialize components
    RAM ram;
    ROM rom;

    if (!rom.loadAppleIIeROMs())
    {
        std::cerr << "Failed to load ROMs\n";
        return 1;
    }

    Keyboard keyboard;
    MMU mmu(ram, rom, &keyboard);

    // Create CPU
    using ReadFunc = std::function<uint8_t(uint16_t)>;
    using WriteFunc = std::function<void(uint16_t, uint8_t)>;
    using CPU = MOS6502::CPU6502<ReadFunc, WriteFunc, MOS6502::CPUVariant::CMOS_65C02>;

    CPU cpu(
        [&mmu](uint16_t address) -> uint8_t { return mmu.read(address); },
        [&mmu](uint16_t address, uint8_t value) -> void { mmu.write(address, value); }
    );

    // Reset CPU
    cpu.reset();
    uint16_t reset_vector = cpu.getPC();
    std::cout << "Reset vector: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(4) << reset_vector << std::dec << "\n\n";

    // Execute until we reach $D01B or get stuck
    std::cout << "Executing until PC reaches $D01B or 1000 instructions...\n\n";

    const int MAX_INSTRUCTIONS = 1000;
    uint16_t last_pc = cpu.getPC();
    int stuck_count = 0;
    bool found_d01b = false;

    for (int i = 0; i < MAX_INSTRUCTIONS; i++)
    {
        uint16_t pc = cpu.getPC();

        // Check if we've reached $D01B
        if (pc == 0xD01B && !found_d01b)
        {
            found_d01b = true;
            std::cout << "Reached $D01B at instruction " << i << "\n\n";
            std::cout << "CPU State:\n";
            std::cout << "  PC: $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(4) << pc << std::dec << "\n";
            std::cout << "  A:  $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)cpu.getA() << std::dec << "\n";
            std::cout << "  X:  $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)cpu.getX() << std::dec << "\n";
            std::cout << "  Y:  $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)cpu.getY() << std::dec << "\n";
            std::cout << "  SP: $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)cpu.getSP() << std::dec << "\n";
            std::cout << "  P:  $" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)cpu.getP() << std::dec << "\n\n";

            // Show the instruction at $D01B and surrounding bytes
            std::cout << "ROM content around $D01B:\n";
            std::cout << "  Address: ";
            for (int j = -8; j < 16; j++)
            {
                if (j == 0) std::cout << " [";
                std::cout << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)mmu.read(0xD01B + j);
                if (j == 0) std::cout << "]";
                std::cout << " ";
            }
            std::cout << std::dec << "\n";
            std::cout << "           ";
            for (int j = -8; j < 16; j++)
            {
                uint16_t addr = 0xD01B + j;
                if (j == 0) std::cout << " ";
                std::cout << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (addr & 0xFF);
                if (j == 0) std::cout << " ";
                std::cout << " ";
            }
            std::cout << std::dec << "\n\n";

            std::cout << "Continuing execution to see if PC stays stuck...\n\n";
        }

        // Execute instruction
        uint32_t cycles = cpu.executeInstruction();

        uint16_t new_pc = cpu.getPC();

        // Check if PC is stuck (not changing)
        if (new_pc == last_pc)
        {
            stuck_count++;
            if (stuck_count == 1 && new_pc == 0xD01B)
            {
                std::cout << "WARNING: PC stuck at $D01B!\n\n";

                uint8_t opcode = mmu.read(new_pc);
                std::cout << "Instruction at $D01B:\n";
                std::cout << "  Opcode: $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)opcode << std::dec << "\n";
                std::cout << "  Cycles consumed: " << cycles << "\n\n";

                std::cout << "CPU State after execution:\n";
                std::cout << "  PC: $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(4) << new_pc << std::dec << "\n";
                std::cout << "  A:  $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)cpu.getA() << std::dec << "\n";
                std::cout << "  X:  $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)cpu.getX() << std::dec << "\n";
                std::cout << "  Y:  $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)cpu.getY() << std::dec << "\n";
                std::cout << "  SP: $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)cpu.getSP() << std::dec << "\n";
                std::cout << "  P:  $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)cpu.getP() << std::dec;

                // Decode flags
                uint8_t p = cpu.getP();
                std::cout << " [";
                std::cout << ((p & 0x80) ? "N" : "n");
                std::cout << ((p & 0x40) ? "V" : "v");
                std::cout << "-";
                std::cout << ((p & 0x10) ? "B" : "b");
                std::cout << ((p & 0x08) ? "D" : "d");
                std::cout << ((p & 0x04) ? "I" : "i");
                std::cout << ((p & 0x02) ? "Z" : "z");
                std::cout << ((p & 0x01) ? "C" : "c");
                std::cout << "]\n\n";

                // Check what's at $C000 (keyboard)
                uint8_t kb_data = mmu.read(0xC000);
                std::cout << "Keyboard status:\n";
                std::cout << "  $C000 (KB_DATA): $" << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << (int)kb_data << std::dec;
                if (kb_data & 0x80)
                    std::cout << " (key available: $" << std::hex << std::uppercase
                              << std::setw(2) << (int)(kb_data & 0x7F) << std::dec << ")";
                else
                    std::cout << " (no key)";
                std::cout << "\n\n";

                // Analyze the opcode
                if (opcode == 0x10)
                {
                    std::cout << "Analysis: Opcode $10 is BPL (Branch if Plus)\n";
                    std::cout << "  This is a conditional branch that checks the N flag.\n";
                    std::cout << "  N flag is: " << ((p & 0x80) ? "SET (1)" : "CLEAR (0)") << "\n";
                    std::cout << "  Branch target: $" << std::hex << std::uppercase << std::setfill('0')
                              << std::setw(4) << (0xD01B + 2 + (int8_t)mmu.read(0xD01C)) << std::dec << "\n";
                }
                else if (opcode == 0xD0)
                {
                    std::cout << "Analysis: Opcode $D0 is BNE (Branch if Not Equal)\n";
                    std::cout << "  This is a conditional branch that checks the Z flag.\n";
                    std::cout << "  Z flag is: " << ((p & 0x02) ? "SET (1)" : "CLEAR (0)") << "\n";
                    std::cout << "  Branch target: $" << std::hex << std::uppercase << std::setfill('0')
                              << std::setw(4) << (0xD01B + 2 + (int8_t)mmu.read(0xD01C)) << std::dec << "\n";
                }
                else if (opcode == 0xF0)
                {
                    std::cout << "Analysis: Opcode $F0 is BEQ (Branch if Equal)\n";
                    std::cout << "  This is a conditional branch that checks the Z flag.\n";
                    std::cout << "  Z flag is: " << ((p & 0x02) ? "SET (1)" : "CLEAR (0)") << "\n";
                    std::cout << "  Branch target: $" << std::hex << std::uppercase << std::setfill('0')
                              << std::setw(4) << (0xD01B + 2 + (int8_t)mmu.read(0xD01C)) << std::dec << "\n";
                }

                // Try executing a few more times to see the pattern
                std::cout << "\nNext 10 instructions:\n";
                for (int j = 0; j < 10; j++)
                {
                    uint16_t exec_pc = cpu.getPC();
                    uint8_t exec_opcode = mmu.read(exec_pc);
                    cpu.executeInstruction();
                    uint16_t next_pc = cpu.getPC();

                    std::cout << "  $" << std::hex << std::uppercase << std::setfill('0')
                              << std::setw(4) << exec_pc << ": $" << std::setw(2) << (int)exec_opcode
                              << " -> $" << std::setw(4) << next_pc << std::dec << "\n";
                }

                return 0;
            }
        }
        else
        {
            stuck_count = 0;
        }

        last_pc = new_pc;
    }

    if (!found_d01b)
    {
        std::cout << "Did not reach $D01B in " << MAX_INSTRUCTIONS << " instructions.\n";
        std::cout << "Final PC: $" << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(4) << cpu.getPC() << std::dec << "\n";
    }
    else
    {
        std::cout << "Completed " << MAX_INSTRUCTIONS << " instructions.\n";
        std::cout << "Final PC: $" << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(4) << cpu.getPC() << std::dec << "\n";
    }

    return 0;
}
