#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <vector>
#include <memory>

// Include the emulator's disk image header
#include "emulator/disk_image.hpp"

// TRANS62 encode table (from DiskImage)
const uint8_t TRANS62[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

// TRANS62 decode table - generated at runtime
uint8_t REV_TRANS62[256];

void initRevTable() {
    memset(REV_TRANS62, 0xFF, 256);
    for (int i = 0; i < 64; i++) {
        REV_TRANS62[TRANS62[i]] = i;
    }
}

// Decode 4-and-4 encoded byte
uint8_t decode44(uint8_t b1, uint8_t b2) {
    return ((b1 & 0x55) << 1) | (b2 & 0x55);
}

int totalErrors = 0;

void test_load(const std::string& diskPath) {
    std::cout << "\n=== TEST: load() ===" << std::endl;

    auto disk = std::make_unique<DiskImage>();

    // Test loading non-existent file
    if (disk->load("/nonexistent/file.dsk")) {
        std::cout << "FAIL: Should not load non-existent file" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Correctly rejected non-existent file" << std::endl;
    }

    // Test loading valid file
    if (!disk->load(diskPath)) {
        std::cout << "FAIL: Failed to load valid disk file" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Successfully loaded disk file" << std::endl;
    }
}

void test_isLoaded(DiskImage* disk) {
    std::cout << "\n=== TEST: isLoaded() ===" << std::endl;

    if (!disk->isLoaded()) {
        std::cout << "FAIL: isLoaded() returned false for loaded disk" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: isLoaded() returned true" << std::endl;
    }

    // Test with unloaded disk
    DiskImage unloaded;
    if (unloaded.isLoaded()) {
        std::cout << "FAIL: isLoaded() returned true for unloaded disk" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Unloaded disk reports not loaded" << std::endl;
    }
}

void test_getFilepath(DiskImage* disk, const std::string& expectedPath) {
    std::cout << "\n=== TEST: getFilepath() ===" << std::endl;

    const std::string& path = disk->getFilepath();
    if (path != expectedPath) {
        std::cout << "FAIL: getFilepath() returned '" << path << "' expected '" << expectedPath << "'" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: getFilepath() returned correct path" << std::endl;
    }
}

void test_getFormat(DiskImage* disk) {
    std::cout << "\n=== TEST: getFormat() ===" << std::endl;

    DiskImage::Format format = disk->getFormat();
    if (format == DiskImage::Format::DSK) {
        std::cout << "PASS: Format is DSK" << std::endl;
    } else if (format == DiskImage::Format::WOZ2) {
        std::cout << "PASS: Format is WOZ2" << std::endl;
    } else {
        std::cout << "FAIL: Unknown format" << std::endl;
        totalErrors++;
    }
}

void test_getNibbleTrackSize(DiskImage* disk) {
    std::cout << "\n=== TEST: getNibbleTrackSize() ===" << std::endl;

    // Test all 35 tracks
    bool allCorrect = true;
    for (int track = 0; track < 35; track++) {
        int size = disk->getNibbleTrackSize(track);
        if (size <= 0) {
            std::cout << "FAIL: Track " << track << " has size " << size << std::endl;
            totalErrors++;
            allCorrect = false;
        }
    }

    if (allCorrect) {
        int size0 = disk->getNibbleTrackSize(0);
        std::cout << "PASS: All 35 tracks have valid sizes (track 0 size: " << size0 << ")" << std::endl;
    }

    // Test invalid tracks
    int invalidSize = disk->getNibbleTrackSize(-1);
    if (invalidSize != 0) {
        std::cout << "FAIL: Track -1 should return size 0, got " << invalidSize << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Invalid track -1 returns size 0" << std::endl;
    }

    invalidSize = disk->getNibbleTrackSize(35);
    if (invalidSize != 0) {
        std::cout << "FAIL: Track 35 should return size 0, got " << invalidSize << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Invalid track 35 returns size 0" << std::endl;
    }
}

void test_getNibble(DiskImage* disk) {
    std::cout << "\n=== TEST: getNibble() ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);

    // Test reading nibbles - all should have bit 7 set (valid disk bytes)
    int invalidCount = 0;
    for (int pos = 0; pos < trackSize; pos++) {
        uint8_t nibble = disk->getNibble(0, pos);
        // All disk bytes should have bit 7 set
        if ((nibble & 0x80) == 0 && nibble != 0x00) {
            invalidCount++;
            if (invalidCount <= 5) {
                std::cout << "Warning: Invalid nibble at pos " << pos << ": 0x"
                          << std::hex << (int)nibble << std::dec << std::endl;
            }
        }
    }

    if (invalidCount == 0) {
        std::cout << "PASS: All " << trackSize << " nibbles have valid bit 7" << std::endl;
    } else {
        std::cout << "FAIL: " << invalidCount << " nibbles have invalid bit 7" << std::endl;
        totalErrors++;
    }

    // Test that position wraps correctly
    uint8_t nibbleStart = disk->getNibble(0, 0);
    uint8_t nibbleWrap = disk->getNibble(0, trackSize);  // Should wrap to position 0
    if (nibbleStart != nibbleWrap) {
        std::cout << "FAIL: Position wrap failed. Pos 0 = 0x" << std::hex << (int)nibbleStart
                  << ", Pos " << std::dec << trackSize << " = 0x" << std::hex << (int)nibbleWrap << std::dec << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: Position wraps correctly at track boundary" << std::endl;
    }

    // Test invalid track
    uint8_t invalidNibble = disk->getNibble(-1, 0);
    if (invalidNibble != 0xFF) {
        std::cout << "Warning: Invalid track -1 returned 0x" << std::hex << (int)invalidNibble
                  << " instead of 0xFF" << std::dec << std::endl;
    } else {
        std::cout << "PASS: Invalid track -1 returns 0xFF" << std::endl;
    }
}

void test_findAddressPrologues(DiskImage* disk) {
    std::cout << "\n=== TEST: Find Address Prologues (D5 AA 96) ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int foundCount = 0;
    std::vector<int> prologuePositions;

    // Scan track 0 for address field prologues
    for (int pos = 0; pos < trackSize - 2; pos++) {
        uint8_t b0 = disk->getNibble(0, pos);
        uint8_t b1 = disk->getNibble(0, pos + 1);
        uint8_t b2 = disk->getNibble(0, pos + 2);

        if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
            prologuePositions.push_back(pos);
            foundCount++;
        }
    }

    if (foundCount == 16) {
        std::cout << "PASS: Found exactly 16 address field prologues on track 0" << std::endl;
    } else {
        std::cout << "FAIL: Expected 16 address field prologues, found " << foundCount << std::endl;
        totalErrors++;
    }

    // Print positions
    std::cout << "  Prologue positions: ";
    for (int pos : prologuePositions) {
        std::cout << pos << " ";
    }
    std::cout << std::endl;
}

void test_findDataPrologues(DiskImage* disk) {
    std::cout << "\n=== TEST: Find Data Prologues (D5 AA AD) ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int foundCount = 0;

    // Scan track 0 for data field prologues
    for (int pos = 0; pos < trackSize - 2; pos++) {
        uint8_t b0 = disk->getNibble(0, pos);
        uint8_t b1 = disk->getNibble(0, pos + 1);
        uint8_t b2 = disk->getNibble(0, pos + 2);

        if (b0 == 0xD5 && b1 == 0xAA && b2 == 0xAD) {
            foundCount++;
        }
    }

    if (foundCount == 16) {
        std::cout << "PASS: Found exactly 16 data field prologues on track 0" << std::endl;
    } else {
        std::cout << "FAIL: Expected 16 data field prologues, found " << foundCount << std::endl;
        totalErrors++;
    }
}

void test_findSectorAtPosition(DiskImage* disk) {
    std::cout << "\n=== TEST: findSectorAtPosition() ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    bool sectorsFound[16] = {false};

    // Find all address prologues and verify findSectorAtPosition works
    for (int pos = 0; pos < trackSize - 2; pos++) {
        uint8_t b0 = disk->getNibble(0, pos);
        uint8_t b1 = disk->getNibble(0, pos + 1);
        uint8_t b2 = disk->getNibble(0, pos + 2);

        if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
            // Use findSectorAtPosition from the data field area (after address field)
            int dataPos = pos + 20;  // Skip past address field
            int sector = disk->findSectorAtPosition(0, dataPos);

            if (sector >= 0 && sector < 16) {
                sectorsFound[sector] = true;
            } else {
                std::cout << "Warning: findSectorAtPosition returned " << sector
                          << " at position " << dataPos << std::endl;
            }
        }
    }

    // Check all 16 sectors were found
    bool allFound = true;
    for (int i = 0; i < 16; i++) {
        if (!sectorsFound[i]) {
            std::cout << "FAIL: Sector " << i << " not found by findSectorAtPosition" << std::endl;
            allFound = false;
            totalErrors++;
        }
    }

    if (allFound) {
        std::cout << "PASS: All 16 sectors found by findSectorAtPosition" << std::endl;
    }
}

void test_decodeAddressFields(DiskImage* disk) {
    std::cout << "\n=== TEST: Decode Address Fields ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    bool sectorsFound[16] = {false};
    int errors = 0;

    // Find all address prologues and decode them
    for (int pos = 0; pos < trackSize - 12; pos++) {
        uint8_t b0 = disk->getNibble(0, pos);
        uint8_t b1 = disk->getNibble(0, pos + 1);
        uint8_t b2 = disk->getNibble(0, pos + 2);

        if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
            // Decode address field (4-and-4 encoded)
            // Format: D5 AA 96 | vol_hi vol_lo | trk_hi trk_lo | sec_hi sec_lo | chk_hi chk_lo | DE AA EB
            int fieldStart = pos + 3;

            uint8_t vol = decode44(disk->getNibble(0, fieldStart), disk->getNibble(0, fieldStart + 1));
            uint8_t trk = decode44(disk->getNibble(0, fieldStart + 2), disk->getNibble(0, fieldStart + 3));
            uint8_t sec = decode44(disk->getNibble(0, fieldStart + 4), disk->getNibble(0, fieldStart + 5));
            uint8_t chk = decode44(disk->getNibble(0, fieldStart + 6), disk->getNibble(0, fieldStart + 7));

            // Verify checksum
            uint8_t expectedChk = vol ^ trk ^ sec;
            if (chk != expectedChk) {
                std::cout << "FAIL: Checksum error at position " << pos
                          << " (expected 0x" << std::hex << (int)expectedChk
                          << ", got 0x" << (int)chk << std::dec << ")" << std::endl;
                errors++;
            }

            // Verify track is 0
            if (trk != 0) {
                std::cout << "FAIL: Track mismatch at position " << pos
                          << " (expected 0, got " << (int)trk << ")" << std::endl;
                errors++;
            }

            // Mark sector as found
            if (sec < 16) {
                sectorsFound[sec] = true;
            }

            // Check epilogue
            uint8_t ep0 = disk->getNibble(0, fieldStart + 8);
            uint8_t ep1 = disk->getNibble(0, fieldStart + 9);
            uint8_t ep2 = disk->getNibble(0, fieldStart + 10);
            if (ep0 != 0xDE || ep1 != 0xAA || ep2 != 0xEB) {
                std::cout << "FAIL: Address epilogue error at position " << pos << std::endl;
                errors++;
            }
        }
    }

    // Check all 16 sectors were found
    for (int i = 0; i < 16; i++) {
        if (!sectorsFound[i]) {
            std::cout << "FAIL: Sector " << i << " not found in address fields" << std::endl;
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "PASS: All 16 address fields decoded correctly with valid checksums" << std::endl;
    }
    totalErrors += errors;
}

void test_decodeDataFields(DiskImage* disk, const std::vector<uint8_t>& rawDiskData) {
    std::cout << "\n=== TEST: Decode Data Fields ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int errors = 0;

    // For each sector on track 0, find and decode the data field
    for (int logical_sector = 0; logical_sector < 16; logical_sector++) {
        // Find address field for this sector
        int addrPos = -1;
        for (int pos = 0; pos < trackSize - 12; pos++) {
            uint8_t b0 = disk->getNibble(0, pos);
            uint8_t b1 = disk->getNibble(0, pos + 1);
            uint8_t b2 = disk->getNibble(0, pos + 2);

            if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
                int fieldStart = pos + 3;
                uint8_t sec = decode44(disk->getNibble(0, fieldStart + 4), disk->getNibble(0, fieldStart + 5));
                if (sec == logical_sector) {
                    addrPos = pos;
                    break;
                }
            }
        }

        if (addrPos < 0) {
            std::cout << "FAIL: Could not find address field for sector " << logical_sector << std::endl;
            errors++;
            continue;
        }

        // Find data field prologue (D5 AA AD) after address field
        int dataPos = -1;
        for (int i = 0; i < 64; i++) {
            int pos = (addrPos + 14 + i) % trackSize;
            uint8_t b0 = disk->getNibble(0, pos);
            uint8_t b1 = disk->getNibble(0, pos + 1);
            uint8_t b2 = disk->getNibble(0, pos + 2);

            if (b0 == 0xD5 && b1 == 0xAA && b2 == 0xAD) {
                dataPos = pos + 3;  // Start of actual data
                break;
            }
        }

        if (dataPos < 0) {
            std::cout << "FAIL: Could not find data field for sector " << logical_sector << std::endl;
            errors++;
            continue;
        }

        // Decode 6-and-2 data
        uint8_t auxBuf[86];
        uint8_t dataBuf[256];
        bool decodeError = false;

        // XOR decode aux nibbles (86 bytes)
        uint8_t prev = 0;
        for (int i = 0; i < 86 && !decodeError; i++) {
            int pos = (dataPos + i) % trackSize;
            uint8_t nibble = disk->getNibble(0, pos);
            uint8_t val = REV_TRANS62[nibble];
            if (val == 0xFF) {
                std::cout << "FAIL: Invalid aux nibble at position " << i << " for sector " << logical_sector << std::endl;
                errors++;
                decodeError = true;
            } else {
                auxBuf[i] = val ^ prev;
                prev = auxBuf[i];
            }
        }

        // XOR decode data nibbles (256 bytes)
        for (int i = 0; i < 256 && !decodeError; i++) {
            int pos = (dataPos + 86 + i) % trackSize;
            uint8_t nibble = disk->getNibble(0, pos);
            uint8_t val = REV_TRANS62[nibble];
            if (val == 0xFF) {
                std::cout << "FAIL: Invalid data nibble at position " << i << " for sector " << logical_sector << std::endl;
                errors++;
                decodeError = true;
            } else {
                dataBuf[i] = val ^ prev;
                prev = dataBuf[i];
            }
        }

        if (decodeError) continue;

        // Verify checksum
        {
            int chkPos = (dataPos + 342) % trackSize;
            uint8_t chkNibble = disk->getNibble(0, chkPos);
            uint8_t chk = REV_TRANS62[chkNibble];
            if (chk != prev) {
                std::cout << "FAIL: Checksum mismatch for sector " << logical_sector
                          << " (expected " << (int)prev << ", got " << (int)chk << ")" << std::endl;
                errors++;
                continue;
            }
        }

        // Reconstruct bytes from aux and data nibbles
        uint8_t decoded[256];
        for (int i = 0; i < 256; i++) {
            uint8_t high6 = dataBuf[i];
            int aux_idx = i % 86;
            uint8_t aux = auxBuf[aux_idx];
            uint8_t low2;

            if (i < 86) {
                low2 = ((aux & 0x02) >> 1) | ((aux & 0x01) << 1);
            } else if (i < 172) {
                low2 = ((aux & 0x08) >> 3) | ((aux & 0x04) >> 1);
            } else {
                low2 = ((aux & 0x20) >> 5) | ((aux & 0x10) >> 3);
            }

            decoded[i] = (high6 << 2) | low2;
        }

        // Compare with original data
        const uint8_t* originalData = &rawDiskData[logical_sector * 256];
        int sectorErrors = 0;
        for (int i = 0; i < 256; i++) {
            if (originalData[i] != decoded[i]) {
                sectorErrors++;
            }
        }

        if (sectorErrors > 0) {
            std::cout << "FAIL: Sector " << logical_sector << " has " << sectorErrors << " byte mismatches" << std::endl;
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "PASS: All 16 data fields decoded correctly" << std::endl;
    }
    totalErrors += errors;
}

void test_writeProtection(DiskImage* disk) {
    std::cout << "\n=== TEST: Write Protection ===" << std::endl;

    bool initialState = disk->isWriteProtected();
    std::cout << "  Initial write protection: " << (initialState ? "ON" : "OFF") << std::endl;

    disk->setWriteProtected(true);
    if (!disk->isWriteProtected()) {
        std::cout << "FAIL: setWriteProtected(true) didn't work" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: setWriteProtected(true) works" << std::endl;
    }

    disk->setWriteProtected(false);
    if (disk->isWriteProtected()) {
        std::cout << "FAIL: setWriteProtected(false) didn't work" << std::endl;
        totalErrors++;
    } else {
        std::cout << "PASS: setWriteProtected(false) works" << std::endl;
    }

    // Restore original state
    disk->setWriteProtected(initialState);
}

void test_allTracks(DiskImage* disk) {
    std::cout << "\n=== TEST: All 35 Tracks ===" << std::endl;

    int errors = 0;

    for (int track = 0; track < 35; track++) {
        int trackSize = disk->getNibbleTrackSize(track);
        if (trackSize <= 0) {
            std::cout << "FAIL: Track " << track << " has invalid size" << std::endl;
            errors++;
            continue;
        }

        // Count address prologues on this track
        int prologueCount = 0;
        for (int pos = 0; pos < trackSize - 2; pos++) {
            uint8_t b0 = disk->getNibble(track, pos);
            uint8_t b1 = disk->getNibble(track, pos + 1);
            uint8_t b2 = disk->getNibble(track, pos + 2);

            if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
                prologueCount++;
            }
        }

        if (prologueCount != 16) {
            std::cout << "FAIL: Track " << track << " has " << prologueCount << " sectors (expected 16)" << std::endl;
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "PASS: All 35 tracks have 16 sectors each" << std::endl;
    }
    totalErrors += errors;
}

// === TIMING TESTS ===
// The Apple II disk spins at 300 RPM (5 rotations per second)
// One rotation = 200ms = 200,000 microseconds
// Apple II clock = 1.023 MHz
// Cycles per rotation = 1,023,000 * 0.2 = 204,600 cycles
// With 6656 nibbles per track: 204,600 / 6656 = 30.74 cycles per nibble

constexpr uint64_t CYCLES_PER_ROTATION = 204600;

// Calculate expected nibble position based on cycles since motor start
int calculateExpectedPosition(uint64_t cycles_on, int trackSize) {
    // nibbles = (cycles * 100) / 3074  (using same formula as disk2.cpp)
    uint64_t total_nibbles = (cycles_on * 100) / 3074;
    return total_nibbles % trackSize;
}

void test_timing_position_calculation(DiskImage* disk) {
    std::cout << "\n=== TEST: Timing Position Calculation ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int errors = 0;

    // Test at various cycle counts
    struct TestCase {
        uint64_t cycles;
        int expectedMinPos;  // Allow small variance
        int expectedMaxPos;
    };

    std::vector<TestCase> tests = {
        {0, 0, 0},              // Start of rotation
        {3074, 100, 100},       // About 100 nibbles in
        {30740, 1000, 1000},    // About 1000 nibbles in
        {CYCLES_PER_ROTATION, 6655, 6656},  // One full rotation
        {CYCLES_PER_ROTATION * 2, 6655, 6656}, // Two rotations
        {CYCLES_PER_ROTATION + 3074, 100, 100}, // One rotation + 100 nibbles
    };

    for (const auto& test : tests) {
        int pos = calculateExpectedPosition(test.cycles, trackSize);

        // The position should match the calculation and be within track bounds
        if (pos < 0 || pos >= trackSize) {
            std::cout << "FAIL: Position out of range at " << test.cycles << " cycles: " << pos << std::endl;
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "PASS: Position calculations are within track bounds" << std::endl;
    }

    // Test position advancement rate
    std::cout << "  Position samples at various cycle counts:" << std::endl;
    uint64_t sampleCycles[] = {0, 1000, 5000, 10000, 50000, 100000, CYCLES_PER_ROTATION, CYCLES_PER_ROTATION * 2};
    for (uint64_t cycles : sampleCycles) {
        int pos = calculateExpectedPosition(cycles, trackSize);
        std::cout << "    Cycles: " << std::setw(8) << cycles
                  << " -> Position: " << pos
                  << " (rotation " << (cycles / CYCLES_PER_ROTATION) << ")" << std::endl;
    }

    totalErrors += errors;
}

void test_timing_sequential_reads(DiskImage* disk) {
    std::cout << "\n=== TEST: Timing Sequential Reads ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int errors = 0;

    // Simulate the DOS RWTS read loop:
    // The ROM polls $C0EC waiting for bit 7 to be set (new nibble ready)
    // This takes about 7 cycles per iteration (LDA $C0EC; BPL wait)
    // When bit 7 is set, it reads the nibble and stores it
    // One nibble arrives every ~31 cycles

    // Simulate reading 343 nibbles (one data field)
    // Starting from position 0, after reading 343 nibbles, we should be at position 343

    uint64_t cycle_count = 0;
    uint64_t motor_start_cycle = 0;

    // Find first address prologue
    int startPos = -1;
    for (int pos = 0; pos < trackSize - 2; pos++) {
        uint8_t b0 = disk->getNibble(0, pos);
        uint8_t b1 = disk->getNibble(0, pos + 1);
        uint8_t b2 = disk->getNibble(0, pos + 2);

        if (b0 == 0xD5 && b1 == 0xAA && b2 == 0x96) {
            startPos = pos;
            break;
        }
    }

    if (startPos < 0) {
        std::cout << "FAIL: Could not find address prologue" << std::endl;
        totalErrors++;
        return;
    }

    // Calculate how many cycles until we reach startPos
    // Position = (cycles * 100 / 3074) % trackSize
    // We want position = startPos, so cycles = startPos * 3074 / 100
    uint64_t target_cycles = (startPos * 3074) / 100;
    cycle_count = target_cycles;

    std::cout << "  Starting at position " << startPos << " (cycle " << cycle_count << ")" << std::endl;

    // Simulate reading the address prologue (D5 AA 96)
    std::vector<uint8_t> readBytes;
    int last_position = -1;

    for (int nibble = 0; nibble < 20 && nibble < 1000; nibble++) {
        // Calculate current position based on cycle count
        uint64_t on_cycles = cycle_count - motor_start_cycle;
        int current_pos = calculateExpectedPosition(on_cycles, trackSize);

        // Wait until position advances (simulating polling loop)
        int poll_iterations = 0;
        while (current_pos == last_position && poll_iterations < 100) {
            cycle_count += 7;  // 7 cycles per poll iteration
            on_cycles = cycle_count - motor_start_cycle;
            current_pos = calculateExpectedPosition(on_cycles, trackSize);
            poll_iterations++;
        }

        if (current_pos == last_position) {
            std::cout << "FAIL: Position didn't advance after 100 poll iterations" << std::endl;
            errors++;
            break;
        }

        // Read the nibble at current position
        uint8_t nibble_byte = disk->getNibble(0, current_pos);
        readBytes.push_back(nibble_byte);
        last_position = current_pos;

        // Add ~10 cycles for storing the nibble
        cycle_count += 10;
    }

    // Verify we read the prologue
    if (readBytes.size() >= 3) {
        bool foundPrologue = false;
        for (size_t i = 0; i + 2 < readBytes.size(); i++) {
            if (readBytes[i] == 0xD5 && readBytes[i+1] == 0xAA && readBytes[i+2] == 0x96) {
                foundPrologue = true;
                std::cout << "  Found address prologue at read offset " << i << std::endl;
                break;
            }
        }

        if (!foundPrologue) {
            std::cout << "FAIL: Did not find D5 AA 96 in read bytes" << std::endl;
            std::cout << "  First 10 bytes read: ";
            for (size_t i = 0; i < 10 && i < readBytes.size(); i++) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)readBytes[i] << " ";
            }
            std::cout << std::dec << std::endl;
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "PASS: Sequential timing reads work correctly" << std::endl;
    }
    totalErrors += errors;
}

void test_timing_rotation_wrap(DiskImage* disk) {
    std::cout << "\n=== TEST: Timing Rotation Wrap ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);
    int errors = 0;

    // Test that after one rotation, we're back to position 0 (or close to it)
    uint64_t cycles = CYCLES_PER_ROTATION;
    int pos = calculateExpectedPosition(cycles, trackSize);

    // After exactly one rotation, we should be very close to start
    // Due to rounding, might not be exactly 0
    if (pos > 10 && pos < trackSize - 10) {
        std::cout << "FAIL: After one rotation (" << CYCLES_PER_ROTATION
                  << " cycles), position is " << pos << " (expected near 0 or " << trackSize << ")" << std::endl;
        errors++;
    } else {
        std::cout << "PASS: Position after one rotation: " << pos << " (wraps correctly)" << std::endl;
    }

    // Test that 2 rotations also wrap correctly
    cycles = CYCLES_PER_ROTATION * 2;
    pos = calculateExpectedPosition(cycles, trackSize);
    if (pos > 10 && pos < trackSize - 10) {
        std::cout << "FAIL: After two rotations, position is " << pos << std::endl;
        errors++;
    } else {
        std::cout << "PASS: Position after two rotations: " << pos << std::endl;
    }

    // Test a fractional rotation
    cycles = CYCLES_PER_ROTATION / 2;  // Half rotation
    pos = calculateExpectedPosition(cycles, trackSize);
    int expectedPos = trackSize / 2;
    int diff = std::abs(pos - expectedPos);
    if (diff > 50) {  // Allow some variance
        std::cout << "FAIL: After half rotation, position is " << pos
                  << " (expected near " << expectedPos << ")" << std::endl;
        errors++;
    } else {
        std::cout << "PASS: Position after half rotation: " << pos
                  << " (expected ~" << expectedPos << ")" << std::endl;
    }

    totalErrors += errors;
}

void test_timing_nibble_rate(DiskImage* disk) {
    std::cout << "\n=== TEST: Timing Nibble Rate ===" << std::endl;

    int trackSize = disk->getNibbleTrackSize(0);

    // Calculate how many cycles to advance by one nibble
    // Expected: ~30.74 cycles per nibble

    int pos1 = calculateExpectedPosition(0, trackSize);
    int pos2 = calculateExpectedPosition(31, trackSize);  // 31 cycles later

    std::cout << "  Position at 0 cycles: " << pos1 << std::endl;
    std::cout << "  Position at 31 cycles: " << pos2 << std::endl;

    // Find exact cycle count where position increments
    int last_pos = calculateExpectedPosition(0, trackSize);
    int increment_cycle = -1;
    for (int c = 1; c < 50; c++) {
        int pos = calculateExpectedPosition(c, trackSize);
        if (pos != last_pos) {
            increment_cycle = c;
            break;
        }
    }

    if (increment_cycle > 0) {
        std::cout << "  Position first increments at cycle " << increment_cycle << std::endl;
        if (increment_cycle >= 30 && increment_cycle <= 32) {
            std::cout << "PASS: Nibble rate is approximately 31 cycles/nibble" << std::endl;
        } else {
            std::cout << "WARNING: Nibble rate is " << increment_cycle
                      << " cycles/nibble (expected ~31)" << std::endl;
        }
    } else {
        std::cout << "FAIL: Position didn't increment in first 50 cycles" << std::endl;
        totalErrors++;
    }
}

int main(int argc, char* argv[]) {
    initRevTable();

    std::string diskPath = "../Apple DOS 3.3 January 1983.dsk";
    if (argc > 1) {
        diskPath = argv[1];
    }

    std::cout << "=== DiskImage Comprehensive Test ===" << std::endl;
    std::cout << "Testing: " << diskPath << std::endl;

    // Read original raw .dsk file for comparison
    std::ifstream rawFile(diskPath, std::ios::binary);
    if (!rawFile) {
        std::cout << "ERROR: Cannot open raw disk file: " << diskPath << std::endl;
        return 1;
    }
    std::vector<uint8_t> rawDiskData(35 * 16 * 256);
    rawFile.read(reinterpret_cast<char*>(rawDiskData.data()), rawDiskData.size());
    rawFile.close();

    // Run load test
    test_load(diskPath);

    // Load disk for remaining tests
    auto disk = std::make_unique<DiskImage>();
    if (!disk->load(diskPath)) {
        std::cout << "ERROR: Failed to load disk for remaining tests" << std::endl;
        return 1;
    }

    // Run remaining tests
    test_isLoaded(disk.get());
    test_getFilepath(disk.get(), diskPath);
    test_getFormat(disk.get());
    test_getNibbleTrackSize(disk.get());
    test_getNibble(disk.get());
    test_findAddressPrologues(disk.get());
    test_findDataPrologues(disk.get());
    test_findSectorAtPosition(disk.get());
    test_decodeAddressFields(disk.get());
    test_decodeDataFields(disk.get(), rawDiskData);
    test_writeProtection(disk.get());
    test_allTracks(disk.get());

    // Timing tests
    test_timing_position_calculation(disk.get());
    test_timing_nibble_rate(disk.get());
    test_timing_rotation_wrap(disk.get());
    test_timing_sequential_reads(disk.get());

    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    if (totalErrors == 0) {
        std::cout << "SUCCESS: All tests passed!" << std::endl;
    } else {
        std::cout << "FAILED: " << totalErrors << " error(s)" << std::endl;
    }

    return totalErrors;
}
