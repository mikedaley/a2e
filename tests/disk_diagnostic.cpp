#include <iostream>
#include <iomanip>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/disk2.hpp"
#include "emulator/disk_image.hpp"

// TRANS62 encoding table (6-bit value -> valid disk nibble)
static const uint8_t TRANS62[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

// DECODE_62: reverse lookup table (disk nibble & 0x7F -> 6-bit value, or 0xFF if invalid)
// Built from TRANS62 by inverting the mapping
static uint8_t DECODE_62[128];
static bool decode_table_initialized = false;

void initDecodeTable() {
    if (decode_table_initialized) return;

    // Initialize all to invalid
    for (int i = 0; i < 128; i++) {
        DECODE_62[i] = 0xFF;
    }

    // Fill in valid mappings from TRANS62
    for (int i = 0; i < 64; i++) {
        uint8_t nibble = TRANS62[i];
        DECODE_62[nibble & 0x7F] = i;
    }

    decode_table_initialized = true;
}

// Global for disk controller
DiskII* g_disk = nullptr;
uint64_t g_cycle = 0;
int g_currentPhase = 0;  // Current stepper phase (0-3)

// Stepper motor phase control
// Phases are at $C0E0-$C0E7 (even=off, odd=on)
// Phase 0: $C0E0/E1, Phase 1: $C0E2/E3, Phase 2: $C0E4/E5, Phase 3: $C0E6/E7
void setPhase(int phase, bool on) {
    uint16_t addr = 0xC0E0 + (phase * 2) + (on ? 1 : 0);
    g_disk->setCycleCount(g_cycle);
    g_disk->read(addr);
    g_cycle += 4;
}

// Move head by one half-track in specified direction
// direction: +1 = inward (higher tracks), -1 = outward (lower tracks)
void stepHead(int direction) {
    // Turn on next phase
    int nextPhase = (g_currentPhase + direction + 4) % 4;
    setPhase(nextPhase, true);

    // Wait for head to move (~100 cycles typical)
    g_cycle += 100;

    // Turn off previous phase
    setPhase(g_currentPhase, false);
    g_cycle += 100;

    g_currentPhase = nextPhase;
}

// Seek to a specific track (0-34)
// Each track is 4 quarter-tracks, each phase step moves 2 quarter-tracks
void seekTrack(int targetTrack) {
    int currentTrack = g_disk->getCurrentTrack(0);

    std::cout << "  Seeking from track " << currentTrack << " to track " << targetTrack << std::endl;

    // Calculate number of half-tracks to move
    // Each full track = 2 half-track steps
    int stepsNeeded = (targetTrack - currentTrack) * 2;
    int direction = (stepsNeeded > 0) ? 1 : -1;
    stepsNeeded = std::abs(stepsNeeded);

    for (int i = 0; i < stepsNeeded; i++) {
        stepHead(direction);
    }

    // Verify we're on the right track
    int actualTrack = g_disk->getCurrentTrack(0);
    if (actualTrack != targetTrack) {
        std::cout << "  WARNING: Expected track " << targetTrack
                  << " but at track " << actualTrack << std::endl;
    }
}

// Read a nibble from $C0EC, advancing cycle count
// This emulates: LDA $C0EC (4 cycles) + BPL loop (3 cycles per retry)
uint8_t readNibbleWithWait(int maxRetries = 1000) {
    for (int i = 0; i < maxRetries; i++) {
        g_disk->setCycleCount(g_cycle);
        uint8_t data = g_disk->read(0xC0EC);
        g_cycle += 4;  // LDA $C0EC

        if (data & 0x80) {
            return data;
        }
        g_cycle += 3;  // BPL loop
    }
    return 0x00;  // Timeout
}

// Decode 4-and-4 encoding (odd-even format)
uint8_t decode44(uint8_t odd, uint8_t even) {
    return ((odd << 1) | 1) & even;
}

// Find address prologue D5 AA 96
bool findAddressPrologue() {
    int state = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t nibble = readNibbleWithWait();
        if (nibble == 0) return false;  // Timeout

        if (state == 0 && nibble == 0xD5) state = 1;
        else if (state == 1 && nibble == 0xAA) state = 2;
        else if (state == 2 && nibble == 0x96) return true;
        else if (nibble == 0xD5) state = 1;
        else state = 0;
    }
    return false;
}

// Find data prologue D5 AA AD
bool findDataPrologue() {
    int state = 0;
    for (int i = 0; i < 50; i++) {  // Data prologue should be close
        uint8_t nibble = readNibbleWithWait();
        if (nibble == 0) return false;

        if (state == 0 && nibble == 0xD5) state = 1;
        else if (state == 1 && nibble == 0xAA) state = 2;
        else if (state == 2 && nibble == 0xAD) return true;
        else if (nibble == 0xD5) state = 1;
        else state = 0;
    }
    return false;
}

// Read and decode address field (8 nibbles: vol, trk, sec, chk - all 4-and-4)
bool readAddressField(uint8_t& volume, uint8_t& track, uint8_t& sector, uint8_t& checksum) {
    uint8_t nibbles[8];
    for (int i = 0; i < 8; i++) {
        nibbles[i] = readNibbleWithWait();
        if (nibbles[i] == 0) return false;
    }

    volume = decode44(nibbles[0], nibbles[1]);
    track = decode44(nibbles[2], nibbles[3]);
    sector = decode44(nibbles[4], nibbles[5]);
    checksum = decode44(nibbles[6], nibbles[7]);

    // Verify checksum
    uint8_t computed = volume ^ track ^ sector;
    if (computed != checksum) {
        std::cout << "  Address checksum error: expected 0x" << std::hex << (int)computed
                  << " got 0x" << (int)checksum << std::dec << std::endl;
        return false;
    }
    return true;
}

// Read and decode data field (343 nibbles -> 256 bytes)
bool readDataField(uint8_t* buffer) {
    uint8_t nibbles[343];

    // Read all 343 nibbles (342 data + 1 checksum)
    for (int i = 0; i < 343; i++) {
        nibbles[i] = readNibbleWithWait();
        if (nibbles[i] == 0) {
            std::cout << "  Timeout reading data nibble " << i << std::endl;
            return false;
        }
        if (!(nibbles[i] & 0x80)) {
            std::cout << "  Invalid nibble at position " << i << ": 0x" << std::hex << (int)nibbles[i] << std::dec << std::endl;
            return false;
        }
    }

    // Decode 6-and-2
    uint8_t aux[86];
    uint8_t data[256];

    // Decode auxiliary buffer (first 86 nibbles)
    uint8_t checksum = 0;
    for (int i = 0; i < 86; i++) {
        uint8_t decoded = DECODE_62[nibbles[i] & 0x7F];
        if (decoded == 0xFF) {
            std::cout << "  Invalid 6-and-2 nibble at aux[" << i << "]: 0x" << std::hex << (int)nibbles[i] << std::dec << std::endl;
            return false;
        }
        aux[i] = decoded ^ checksum;
        checksum = aux[i];
    }

    // Decode main data (next 256 nibbles)
    for (int i = 0; i < 256; i++) {
        uint8_t decoded = DECODE_62[nibbles[86 + i] & 0x7F];
        if (decoded == 0xFF) {
            std::cout << "  Invalid 6-and-2 nibble at data[" << i << "]: 0x" << std::hex << (int)nibbles[86 + i] << std::dec << std::endl;
            return false;
        }
        data[i] = decoded ^ checksum;
        checksum = data[i];
    }

    // Verify final checksum nibble
    uint8_t ckDecoded = DECODE_62[nibbles[342] & 0x7F];
    if (ckDecoded == 0xFF || (ckDecoded ^ checksum) != 0) {
        std::cout << "  Data checksum error" << std::endl;
        return false;
    }

    // Combine aux bits with data bytes to form final 256 bytes
    for (int i = 0; i < 256; i++) {
        // Each aux byte holds 2 bits for 3 data bytes
        int auxIdx = i % 86;
        int bitPos = (i / 86);  // 0, 1, or 2

        uint8_t auxBits;
        if (bitPos == 0) {
            auxBits = (aux[auxIdx] & 0x03);
        } else if (bitPos == 1) {
            auxBits = (aux[auxIdx] & 0x0C) >> 2;
        } else {
            auxBits = (aux[auxIdx] & 0x30) >> 4;
        }

        buffer[i] = (data[i] << 2) | auxBits;
    }

    return true;
}

// Read a specific sector (like the boot ROM would)
bool readSector(int wantTrack, int wantSector, uint8_t* buffer) {
    // Find the sector (may need to search for a while)
    for (int attempt = 0; attempt < 20; attempt++) {  // Max 20 sectors to check
        // Find address prologue
        if (!findAddressPrologue()) {
            std::cout << "  Failed to find address prologue" << std::endl;
            return false;
        }

        // Read address field
        uint8_t volume, track, sector, checksum;
        if (!readAddressField(volume, track, sector, checksum)) {
            std::cout << "  Failed to read address field" << std::endl;
            continue;  // Try next sector
        }

        std::cout << "  Found sector: Vol=" << (int)volume
                  << " Trk=" << (int)track
                  << " Sec=" << (int)sector << std::endl;

        // Check if this is the sector we want
        if (track == wantTrack && sector == wantSector) {
            // Find data prologue
            if (!findDataPrologue()) {
                std::cout << "  Failed to find data prologue" << std::endl;
                return false;
            }

            // Read and decode data
            if (!readDataField(buffer)) {
                std::cout << "  Failed to read data field" << std::endl;
                return false;
            }

            return true;
        }

        // Not our sector, skip data field and continue
    }

    std::cout << "  Sector not found after 20 attempts" << std::endl;
    return false;
}

int main(int argc, char* argv[]) {
    // Initialize decode table
    initDecodeTable();

    std::string diskPath = "/Users/michaeldaley/Source/a2e/Apple DOS 3.3 January 1983.dsk";
    if (argc > 1) {
        diskPath = argv[1];
    }

    std::cout << "=== Boot ROM Emulation Diagnostic ===" << std::endl;
    std::cout << "Testing: " << diskPath << std::endl << std::endl;

    // Create disk controller
    DiskII disk;
    g_disk = &disk;

    // Load disk image
    auto diskImage = std::make_unique<DiskImage>();
    if (!diskImage->load(diskPath)) {
        std::cout << "ERROR: Failed to load disk image" << std::endl;
        return 1;
    }

    // Insert disk in drive 0
    if (!disk.insertDisk(0, std::move(diskImage))) {
        std::cout << "ERROR: Failed to insert disk" << std::endl;
        return 1;
    }

    std::cout << "Disk loaded and inserted" << std::endl << std::endl;

    // Initialize - emulate boot ROM start
    g_cycle = 0;

    // Step 1: Turn on motor ($C0E9)
    std::cout << "=== Step 1: Turn on motor ($C0E9) ===" << std::endl;
    disk.setCycleCount(g_cycle);
    disk.read(0xC0E9);
    g_cycle += 4;

    // Step 2: Select drive 1 ($C0EA)
    std::cout << "=== Step 2: Select drive 1 ($C0EA) ===" << std::endl;
    disk.setCycleCount(g_cycle);
    disk.read(0xC0EA);
    g_cycle += 4;

    // Step 3: Set read mode ($C0EE)
    std::cout << "=== Step 3: Set read mode ($C0EE) ===" << std::endl;
    disk.setCycleCount(g_cycle);
    disk.read(0xC0EE);
    g_cycle += 4;

    // Step 4: Read sector 0 of track 0 (boot sector)
    std::cout << std::endl << "=== Step 4: Reading boot sector (T0 S0) ===" << std::endl;

    uint8_t bootSector[256];
    if (!readSector(0, 0, bootSector)) {
        std::cout << "FAILED to read boot sector!" << std::endl;
        return 1;
    }

    std::cout << std::endl << "=== Boot sector successfully read! ===" << std::endl;
    std::cout << "First 64 bytes:" << std::endl;
    for (int i = 0; i < 64; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)bootSector[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << std::endl;
    }
    std::cout << std::dec << std::endl;

    // Verify boot sector starts with 01 (JSR instruction for DOS 3.3)
    if (bootSector[0] == 0x01) {
        std::cout << "Boot sector signature OK (DOS 3.3 boot sector)" << std::endl;
    } else {
        std::cout << "WARNING: Boot sector byte 0 is 0x" << std::hex << (int)bootSector[0]
                  << " (expected 0x01 for DOS 3.3)" << std::dec << std::endl;
    }

    // Step 5: Test track seeking and reading from multiple tracks
    std::cout << std::endl << "=== Step 5: Testing track seeking ===" << std::endl;

    // Test seeking to track 17 (catalog track for DOS 3.3)
    std::cout << "\nSeeking to track 17 (DOS 3.3 catalog)..." << std::endl;
    seekTrack(17);

    uint8_t catalogSector[256];
    if (!readSector(17, 0, catalogSector)) {
        std::cout << "FAILED to read catalog sector (T17 S0)!" << std::endl;
        return 1;
    }
    std::cout << "  Catalog sector read OK - First 16 bytes:" << std::endl << "  ";
    for (int i = 0; i < 16; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)catalogSector[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // Test seeking back to track 0
    std::cout << "\nSeeking back to track 0..." << std::endl;
    seekTrack(0);

    uint8_t verifySector[256];
    if (!readSector(0, 0, verifySector)) {
        std::cout << "FAILED to re-read boot sector (T0 S0)!" << std::endl;
        return 1;
    }

    // Verify it matches what we read before
    bool match = true;
    for (int i = 0; i < 256; i++) {
        if (verifySector[i] != bootSector[i]) {
            match = false;
            std::cout << "MISMATCH at byte " << i << ": expected 0x"
                      << std::hex << (int)bootSector[i] << " got 0x" << (int)verifySector[i]
                      << std::dec << std::endl;
            break;
        }
    }
    if (match) {
        std::cout << "  Boot sector re-read OK - data matches original read" << std::endl;
    }

    // Step 6: Test seeking to the last track
    std::cout << "\nSeeking to track 34 (last track)..." << std::endl;
    seekTrack(34);

    uint8_t lastTrackSector[256];
    if (!readSector(34, 0, lastTrackSector)) {
        std::cout << "FAILED to read last track sector (T34 S0)!" << std::endl;
        return 1;
    }
    std::cout << "  Last track sector read OK - First 16 bytes:" << std::endl << "  ";
    for (int i = 0; i < 16; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)lastTrackSector[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // Step 7: Test reading multiple sectors from a track
    std::cout << "\n=== Step 7: Reading all 16 sectors from track 1 ===" << std::endl;
    seekTrack(1);

    int sectorsRead = 0;
    for (int sec = 0; sec < 16; sec++) {
        uint8_t buffer[256];
        if (readSector(1, sec, buffer)) {
            sectorsRead++;
        } else {
            std::cout << "  FAILED to read T1 S" << sec << std::endl;
        }
    }
    std::cout << "  Read " << sectorsRead << "/16 sectors from track 1" << std::endl;

    if (sectorsRead != 16) {
        std::cout << "\nFAILED: Could not read all sectors" << std::endl;
        return 1;
    }

    std::cout << std::endl << "=== ALL TESTS PASSED ===" << std::endl;
    std::cout << "Track seeking and multi-sector reads working correctly" << std::endl;
    std::cout << "Total cycles: " << g_cycle << std::endl;

    return 0;
}
