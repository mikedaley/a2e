// Headless boot test - runs the emulator to trace DOS boot sequence
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory>
#include <array>
#include <functional>

#include "emulator/ram.hpp"
#include "emulator/rom.hpp"
#include "emulator/mmu.hpp"
#include "emulator/disk2.hpp"
#include "emulator/disk_image.hpp"
#include <MOS6502/CPU6502.hpp>

// Global pointers for CPU callbacks
MMU* g_mmu = nullptr;

uint8_t cpuRead(uint16_t address) {
    return g_mmu->read(address);
}

void cpuWrite(uint16_t address, uint8_t value) {
    g_mmu->write(address, value);
}

int main(int argc, char* argv[]) {
    std::string diskPath = "/Users/michaeldaley/Source/a2e/Apple DOS 3.3 January 1983.dsk";
    if (argc > 1) {
        diskPath = argv[1];
    }

    std::cout << "=== Headless Boot Test ===" << std::endl;
    std::cout << "Disk: " << diskPath << std::endl << std::endl;

    // Create components
    auto ram = std::make_unique<RAM>();
    auto rom = std::make_unique<ROM>();
    auto disk_ii = std::make_unique<DiskII>();

    // Load ROMs
    if (!rom->loadAppleIIeROMs()) {
        std::cerr << "Failed to load system ROMs" << std::endl;
        return 1;
    }

    // Load Disk II ROM
    if (!rom->loadDiskIIROM("resources/roms/peripheral/341-0027.bin")) {
        std::cerr << "Failed to load Disk II ROM" << std::endl;
        return 1;
    }

    // Load disk image
    auto diskImage = std::make_unique<DiskImage>();
    if (!diskImage->load(diskPath)) {
        std::cerr << "Failed to load disk image" << std::endl;
        return 1;
    }

    disk_ii->insertDisk(0, std::move(diskImage));

    // Create MMU
    auto mmu = std::make_unique<MMU>(*ram, *rom, nullptr, nullptr, disk_ii.get());
    g_mmu = mmu.get();

    // Create CPU (65C02 variant)
    MOS6502::CPU6502<decltype(&cpuRead), decltype(&cpuWrite), MOS6502::CPUVariant::CMOS_65C02> cpu(cpuRead, cpuWrite);

    // Reset CPU
    cpu.reset();
    std::cout << "CPU reset, PC = $" << std::hex << cpu.getPC() << std::dec << std::endl;

    // Track execution
    uint64_t instructionCount = 0;
    uint64_t maxInstructions = 5000000;  // Run for 5 million instructions
    uint16_t lastPC = 0;
    int samePC_count = 0;

    // Track interesting addresses
    bool enteredC600 = false;
    bool enteredBoot1 = false;
    bool enteredRWTS = false;

    // Open trace file for detailed logging
    std::ofstream trace("/tmp/boot_trace.txt");
    trace << "Boot trace log\n\n";

    while (instructionCount < maxInstructions) {
        uint16_t pc = cpu.getPC();

        // Update cycle count for disk timing
        mmu->setCycleCount(cpu.getTotalCycles());

        // Log key events
        if (pc >= 0xC600 && pc <= 0xC6FF && !enteredC600) {
            enteredC600 = true;
            std::cout << "Entered Disk II boot ROM at $C600 (instr " << instructionCount << ")" << std::endl;
            trace << "*** Entered $C600 at instruction " << instructionCount << "\n";
        }

        if (pc >= 0x0800 && pc <= 0x08FF && !enteredBoot1) {
            enteredBoot1 = true;
            std::cout << "Entered Boot1 code at $0800 (instr " << instructionCount << ")" << std::endl;
            trace << "*** Entered $0800 at instruction " << instructionCount << "\n";

            // Dump boot sector to verify it loaded correctly
            std::cout << "\nBoot sector at $0800-$081F:" << std::endl;
            for (int row = 0; row < 2; row++) {
                std::cout << std::hex << std::setw(4) << std::setfill('0') << (0x0800 + row*16) << ": ";
                for (int col = 0; col < 16; col++) {
                    std::cout << std::setw(2) << (int)mmu->read(0x0800 + row*16 + col) << " ";
                }
                std::cout << std::dec << std::endl;
            }

            // Dump critical zeropage values
            std::cout << "\nZeropage at Boot1 entry:" << std::endl;
            std::cout << "  $26=" << std::hex << std::setw(2) << (int)mmu->read(0x26)
                      << " $27=" << std::setw(2) << (int)mmu->read(0x27)
                      << " $2B=" << std::setw(2) << (int)mmu->read(0x2B)
                      << " $3C=" << std::setw(2) << (int)mmu->read(0x3C)
                      << " $3D=" << std::setw(2) << (int)mmu->read(0x3D)
                      << " $3E=" << std::setw(2) << (int)mmu->read(0x3E)
                      << " $3F=" << std::setw(2) << (int)mmu->read(0x3F)
                      << " $40=" << std::setw(2) << (int)mmu->read(0x40)
                      << " $41=" << std::setw(2) << (int)mmu->read(0x41)
                      << std::dec << std::endl;

            // Dump $08FE-$08FF (sector count in boot sector)
            std::cout << "  $08FE=" << std::hex << std::setw(2) << (int)mmu->read(0x08FE)
                      << " $08FF=" << std::setw(2) << (int)mmu->read(0x08FF)
                      << std::dec << std::endl;
            std::cout << std::endl;
        }

        if (pc >= 0xB800 && pc <= 0xBFFF && !enteredRWTS) {
            enteredRWTS = true;
            std::cout << "Entered RWTS area at $" << std::hex << pc << std::dec
                      << " (instr " << instructionCount << ")" << std::endl;
            trace << "*** Entered RWTS at instruction " << instructionCount << "\n";
        }

        // Check for relocated RWTS entry at $3700
        if (pc == 0x3700) {
            std::cout << "Entered RELOCATED RWTS at $3700 (instr " << instructionCount << ")" << std::endl;
            trace << "*** Entered relocated RWTS $3700 at instruction " << instructionCount << "\n";
        }

        // Check for crash at $36FF (disk error handler)
        if (pc == 0x36FF) {
            std::cout << "\n*** CRASH at $36FF (disk error) ***" << std::endl;
            std::cout << "A=$" << std::hex << (int)cpu.getA()
                      << " X=$" << (int)cpu.getX()
                      << " Y=$" << (int)cpu.getY()
                      << " SP=$" << (int)cpu.getSP()
                      << std::dec << std::endl;
            std::cout << "Instruction count: " << instructionCount << std::endl;
            std::cout << "Total cycles: " << cpu.getTotalCycles() << std::endl;

            // Dump stack
            std::cout << "\nStack dump:" << std::endl;
            for (int i = 0; i < 16; i++) {
                uint16_t addr = 0x0100 + cpu.getSP() + 1 + i;
                if (addr > 0x01FF) break;
                std::cout << std::hex << std::setw(4) << addr << ": "
                          << std::setw(2) << std::setfill('0') << (int)mmu->read(addr)
                          << std::setfill(' ') << std::dec << std::endl;
            }
            break;
        }

        // Check for monitor entry (typically $FF69 is MONZ)
        if (pc == 0xFF69) {
            std::cout << "\n*** Entered MONITOR at $FF69 ***" << std::endl;

            // This is a RELOCATED DOS - sectors load to $3600-$3FFF, not $B600-$BFFF
            std::cout << "\nMemory at $3600-$361F (should have BOOT1 copy):" << std::endl;
            for (int row = 0; row < 2; row++) {
                std::cout << std::hex << std::setw(4) << std::setfill('0') << (0x3600 + row*16) << ": ";
                for (int col = 0; col < 16; col++) {
                    std::cout << std::setw(2) << (int)mmu->read(0x3600 + row*16 + col) << " ";
                }
                std::cout << std::dec << std::endl;
            }

            std::cout << "\nMemory at $3700-$371F (RWTS entry):" << std::endl;
            for (int row = 0; row < 2; row++) {
                std::cout << std::hex << std::setw(4) << std::setfill('0') << (0x3700 + row*16) << ": ";
                for (int col = 0; col < 16; col++) {
                    std::cout << std::setw(2) << (int)mmu->read(0x3700 + row*16 + col) << " ";
                }
                std::cout << std::dec << std::endl;
            }

            std::cout << "\nMemory at $3F00-$3F1F (last sector loaded):" << std::endl;
            for (int row = 0; row < 2; row++) {
                std::cout << std::hex << std::setw(4) << std::setfill('0') << (0x3F00 + row*16) << ": ";
                for (int col = 0; col < 16; col++) {
                    std::cout << std::setw(2) << (int)mmu->read(0x3F00 + row*16 + col) << " ";
                }
                std::cout << std::dec << std::endl;
            }

            // Also dump $0900 for comparison
            std::cout << "\nMemory at $0900-$091F (old check location):" << std::endl;
            for (int row = 0; row < 2; row++) {
                std::cout << std::hex << std::setw(4) << std::setfill('0') << (0x0900 + row*16) << ": ";
                for (int col = 0; col < 16; col++) {
                    std::cout << std::setw(2) << (int)mmu->read(0x0900 + row*16 + col) << " ";
                }
                std::cout << std::dec << std::endl;
            }
            std::cout << "A=$" << std::hex << (int)cpu.getA()
                      << " X=$" << (int)cpu.getX()
                      << " Y=$" << (int)cpu.getY()
                      << " SP=$" << (int)cpu.getSP()
                      << std::dec << std::endl;
            std::cout << "Instruction count: " << instructionCount << std::endl;

            // Dump return address from stack
            uint16_t retAddr = mmu->read(0x0100 + cpu.getSP() + 1) |
                              (mmu->read(0x0100 + cpu.getSP() + 2) << 8);
            std::cout << "Return address: $" << std::hex << retAddr << std::dec << std::endl;

            // Dump more stack
            std::cout << "\nStack (from SP+1):" << std::endl;
            for (int i = 0; i < 24 && (0x0100 + cpu.getSP() + 1 + i) <= 0x01FF; i++) {
                uint16_t addr = 0x0100 + cpu.getSP() + 1 + i;
                std::cout << std::hex << std::setw(4) << addr << ": "
                          << std::setw(2) << std::setfill('0') << (int)mmu->read(addr)
                          << std::setfill(' ') << std::dec << std::endl;
            }

            // Dump zeropage for debugging
            std::cout << "\nZeropage $00-$4F:" << std::endl;
            for (int i = 0; i < 0x50; i++) {
                if (i % 16 == 0) std::cout << std::hex << std::setw(2) << std::setfill('0') << i << ": ";
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mmu->read(i) << " ";
                if (i % 16 == 15) std::cout << std::endl;
            }
            std::cout << std::dec << std::endl;
        }

        // Log the last 100 instructions before any monitor/crash
        static std::array<std::string, 100> lastInstructions;
        static int logIndex = 0;

        // When entering monitor, dump last 100 instructions
        if (pc == 0xFF69) {
            std::cout << "\nLast 100 instructions before monitor:" << std::endl;
            for (int i = 0; i < 100; i++) {
                int idx = (logIndex + i) % 100;
                if (!lastInstructions[idx].empty()) {
                    std::cout << lastInstructions[idx] << std::endl;
                }
            }
            break;
        }
        std::ostringstream oss;
        oss << std::hex << std::setw(4) << std::setfill('0') << pc
            << " A=" << std::setw(2) << (int)cpu.getA()
            << " X=" << std::setw(2) << (int)cpu.getX()
            << " Y=" << std::setw(2) << (int)cpu.getY()
            << " SP=" << std::setw(2) << (int)cpu.getSP();
        lastInstructions[logIndex % 100] = oss.str();
        logIndex++;

        // Check for infinite loop (same PC repeatedly)
        if (pc == lastPC) {
            samePC_count++;
            if (samePC_count > 100) {
                std::cout << "\n*** INFINITE LOOP at $" << std::hex << pc << std::dec << std::endl;
                std::cout << "A=$" << std::hex << (int)cpu.getA()
                          << " X=$" << (int)cpu.getX()
                          << " Y=$" << (int)cpu.getY()
                          << std::dec << std::endl;
                break;
            }
        } else {
            samePC_count = 0;
        }
        lastPC = pc;

        // Log every 100000 instructions
        if (instructionCount % 100000 == 0 && instructionCount > 0) {
            std::cout << "Progress: " << instructionCount << " instructions, PC=$"
                      << std::hex << pc << std::dec
                      << ", cycles=" << cpu.getTotalCycles() << std::endl;
        }

        // Detailed trace - log disk ROM and Boot1 execution
        // Note: This disk has RELOCATED DOS at $3600-$3FFF (not $B600-$BFFF)
        if ((pc >= 0xC600 && pc <= 0xC6FF) ||
            (pc >= 0x0800 && pc <= 0x09FF) ||
            (pc >= 0x3600 && pc <= 0x3FFF) ||  // Relocated DOS
            (pc >= 0xB600 && pc <= 0xBFFF)) {  // Standard RWTS (keep for other disks)
            trace << std::hex << std::setw(4) << std::setfill('0') << pc
                  << " A=" << std::setw(2) << (int)cpu.getA()
                  << " X=" << std::setw(2) << (int)cpu.getX()
                  << " Y=" << std::setw(2) << (int)cpu.getY()
                  << std::dec << "\n";
        }

        // Log disk controller accesses
        static uint16_t lastDiskAccess = 0;
        if (pc >= 0xC600 && pc <= 0xC6FF) {
            // In disk ROM, log reads from $C0EC
        }

        // Execute one instruction
        (void)cpu.executeInstruction();
        instructionCount++;
    }

    if (instructionCount >= maxInstructions) {
        std::cout << "\nReached max instruction limit (" << maxInstructions << ")" << std::endl;
        std::cout << "Final PC=$" << std::hex << cpu.getPC() << std::dec << std::endl;
    }

    trace.close();
    std::cout << "\nTrace saved to /tmp/boot_trace.txt" << std::endl;

    return 0;
}
