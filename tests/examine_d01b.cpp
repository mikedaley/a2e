#include <iostream>
#include <iomanip>
#include "rom.hpp"

int main()
{
    std::cout << "Examining ROM at $D01B\n";
    std::cout << "======================\n\n";

    ROM rom;
    if (!rom.loadAppleIIeROMs())
    {
        std::cerr << "Failed to load ROMs\n";
        return 1;
    }

    std::cout << "ROM Content around $D01B:\n";
    std::cout << "Address  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n";
    std::cout << "---------------------------------------------------------\n";

    // Show 16 rows (256 bytes) centered around $D01B
    for (uint16_t addr = 0xD000; addr < 0xD100; addr += 16)
    {
        std::cout << "$" << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(4) << addr << " : ";

        for (int i = 0; i < 16; i++)
        {
            uint16_t read_addr = addr + i;
            uint8_t byte = rom.read(read_addr);

            if (read_addr == 0xD01B)
                std::cout << "[" << std::setw(2) << (int)byte << "]";
            else
                std::cout << " " << std::setw(2) << (int)byte << " ";
        }
        std::cout << std::dec << "\n";
    }

    std::cout << "\nInstruction at $D01B:\n";
    uint8_t opcode = rom.read(0xD01B);
    uint8_t operand = rom.read(0xD01C);

    std::cout << "  Opcode: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)opcode << std::dec << "\n";
    std::cout << "  Operand: $" << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)operand << std::dec << "\n";

    // Decode common opcodes
    std::cout << "\nOpcode analysis:\n";
    switch (opcode)
    {
        case 0x10:
            std::cout << "  BPL (Branch if Plus) - relative offset: $"
                      << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)operand
                      << " (branch to $" << std::setw(4)
                      << (0xD01B + 2 + (int8_t)operand) << ")" << std::dec << "\n";
            break;
        case 0x30:
            std::cout << "  BMI (Branch if Minus) - relative offset: $"
                      << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)operand
                      << " (branch to $" << std::setw(4)
                      << (0xD01B + 2 + (int8_t)operand) << ")" << std::dec << "\n";
            break;
        case 0xD0:
            std::cout << "  BNE (Branch if Not Equal) - relative offset: $"
                      << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)operand
                      << " (branch to $" << std::setw(4)
                      << (0xD01B + 2 + (int8_t)operand) << ")" << std::dec << "\n";
            std::cout << "  This creates an infinite loop if Z flag is always clear\n";
            break;
        case 0xF0:
            std::cout << "  BEQ (Branch if Equal) - relative offset: $"
                      << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(2) << (int)operand
                      << " (branch to $" << std::setw(4)
                      << (0xD01B + 2 + (int8_t)operand) << ")" << std::dec << "\n";
            std::cout << "  This creates an infinite loop if Z flag is always set\n";
            break;
        case 0x4C:
            std::cout << "  JMP (absolute) to $" << std::hex << std::uppercase
                      << std::setfill('0') << std::setw(2) << (int)rom.read(0xD01C + 1)
                      << std::setw(2) << (int)operand << std::dec << "\n";
            break;
        case 0xAD:
            std::cout << "  LDA (absolute) from $" << std::hex << std::uppercase
                      << std::setfill('0') << std::setw(2) << (int)rom.read(0xD01C + 1)
                      << std::setw(2) << (int)operand << std::dec << "\n";
            break;
        case 0x2C:
            std::cout << "  BIT (absolute) test $" << std::hex << std::uppercase
                      << std::setfill('0') << std::setw(2) << (int)rom.read(0xD01C + 1)
                      << std::setw(2) << (int)operand << std::dec << "\n";
            break;
        default:
            std::cout << "  Unknown or uncommon opcode\n";
            break;
    }

    return 0;
}
