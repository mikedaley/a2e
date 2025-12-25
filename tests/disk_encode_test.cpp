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

// Read nibble from DiskImage at given position
uint8_t getNibbleAt(DiskImage* disk, int track, int pos) {
    int trackSize = disk->getNibbleTrackSize(track);
    return disk->getNibble(track, pos % trackSize);
}

// Find address field prologue (D5 AA 96) starting from given position
int findAddressPrologue(DiskImage* disk, int track, int startPos) {
    int size = disk->getNibbleTrackSize(track);
    for (int i = 0; i < size; i++) {
        int pos = (startPos + i) % size;
        if (getNibbleAt(disk, track, pos) == 0xD5 &&
            getNibbleAt(disk, track, pos + 1) == 0xAA &&
            getNibbleAt(disk, track, pos + 2) == 0x96) {
            return pos;
        }
    }
    return -1;
}

// Find data field prologue (D5 AA AD) after given position
int findDataPrologue(DiskImage* disk, int track, int afterPos) {
    int size = disk->getNibbleTrackSize(track);
    // Search up to 64 bytes after address field
    for (int i = 0; i < 64; i++) {
        int pos = (afterPos + i) % size;
        if (getNibbleAt(disk, track, pos) == 0xD5 &&
            getNibbleAt(disk, track, pos + 1) == 0xAA &&
            getNibbleAt(disk, track, pos + 2) == 0xAD) {
            return pos;
        }
    }
    return -1;
}

// Decode 6-and-2 data (343 nibbles -> 256 bytes)
bool decode62(DiskImage* disk, int track, int startPos, uint8_t* output) {
    int size = disk->getNibbleTrackSize(track);

    uint8_t auxBuf[86];
    uint8_t dataBuf[256];

    // XOR decode aux nibbles
    uint8_t prev = 0;
    for (int i = 0; i < 86; i++) {
        int pos = (startPos + i) % size;
        uint8_t nibble = getNibbleAt(disk, track, pos);
        uint8_t val = REV_TRANS62[nibble];
        if (val == 0xFF) {
            std::cout << "Invalid nibble at aux " << i << ": 0x" << std::hex
                      << (int)nibble << std::dec << std::endl;
            return false;
        }
        auxBuf[i] = val ^ prev;
        prev = auxBuf[i];
    }

    // XOR decode data nibbles
    for (int i = 0; i < 256; i++) {
        int pos = (startPos + 86 + i) % size;
        uint8_t nibble = getNibbleAt(disk, track, pos);
        uint8_t val = REV_TRANS62[nibble];
        if (val == 0xFF) {
            std::cout << "Invalid nibble at data " << i << ": 0x" << std::hex
                      << (int)nibble << std::dec << std::endl;
            return false;
        }
        dataBuf[i] = val ^ prev;
        prev = dataBuf[i];
    }

    // Verify checksum
    int chkPos = (startPos + 342) % size;
    uint8_t chkNibble = getNibbleAt(disk, track, chkPos);
    uint8_t chk = REV_TRANS62[chkNibble];
    if (chk != prev) {
        std::cout << "Checksum mismatch: expected " << (int)prev << " got " << (int)chk << std::endl;
        return false;
    }

    // Reconstruct bytes from aux and data nibbles
    // Disk II ROM decoding reverses bits within each 2-bit group
    for (int i = 0; i < 256; i++) {
        uint8_t high6 = dataBuf[i];
        int aux_idx = i % 86;
        uint8_t aux = auxBuf[aux_idx];
        uint8_t low2;

        if (i < 86) {
            // Bits 0,1 of aux, reversed
            low2 = ((aux & 0x02) >> 1) | ((aux & 0x01) << 1);
        } else if (i < 172) {
            // Bits 2,3 of aux, reversed
            low2 = ((aux & 0x08) >> 3) | ((aux & 0x04) >> 1);
        } else {
            // Bits 4,5 of aux, reversed
            low2 = ((aux & 0x20) >> 5) | ((aux & 0x10) >> 3);
        }

        output[i] = (high6 << 2) | low2;
    }

    return true;
}

int main(int argc, char* argv[]) {
    initRevTable();  // Initialize reverse lookup table

    std::string diskPath = "../Apple DOS 3.3 January 1983.dsk";
    if (argc > 1) {
        diskPath = argv[1];
    }

    std::cout << "=== Disk Image Encode/Decode Test ===" << std::endl;
    std::cout << "Testing: " << diskPath << std::endl << std::endl;

    // Read original raw .dsk file for comparison
    std::ifstream rawFile(diskPath, std::ios::binary);
    if (!rawFile) {
        std::cout << "ERROR: Cannot open raw disk file: " << diskPath << std::endl;
        return 1;
    }
    std::vector<uint8_t> rawDiskData(35 * 16 * 256);
    rawFile.read(reinterpret_cast<char*>(rawDiskData.data()), rawDiskData.size());
    rawFile.close();

    // Load disk using emulator's DiskImage class
    auto diskImage = std::make_unique<DiskImage>();
    if (!diskImage->load(diskPath)) {
        std::cout << "ERROR: Failed to load disk image via DiskImage class" << std::endl;
        return 1;
    }

    std::cout << "Disk loaded successfully: " << diskImage->getFilepath() << std::endl;
    std::cout << "Track size: " << diskImage->getNibbleTrackSize(0) << " nibbles" << std::endl;
    std::cout << std::endl;

    int totalErrors = 0;

    // Test all 16 sectors on track 0
    for (int logical_sector = 0; logical_sector < 16; logical_sector++) {
        std::cout << "--- Testing Sector " << logical_sector << " ---" << std::endl;

        // Get original sector data from raw .dsk file
        const uint8_t* originalData = &rawDiskData[logical_sector * 256];

        // Find this sector in the nibble track by scanning address fields
        int searchPos = 0;
        bool found = false;

        for (int attempt = 0; attempt < 16; attempt++) {
            int addrPos = findAddressPrologue(diskImage.get(), 0, searchPos);
            if (addrPos < 0) {
                break;
            }

            // Decode address field (starts 3 bytes after prologue start)
            int fieldStart = addrPos + 3;
            uint8_t track_num = decode44(getNibbleAt(diskImage.get(), 0, fieldStart + 2),
                                         getNibbleAt(diskImage.get(), 0, fieldStart + 3));
            uint8_t sector_num = decode44(getNibbleAt(diskImage.get(), 0, fieldStart + 4),
                                          getNibbleAt(diskImage.get(), 0, fieldStart + 5));

            if (sector_num == logical_sector && track_num == 0) {
                // Found our sector - now find its data field
                int dataPos = findDataPrologue(diskImage.get(), 0, addrPos + 14);
                if (dataPos < 0) {
                    std::cout << "ERROR: Could not find data field for sector " << logical_sector << std::endl;
                    totalErrors++;
                    break;
                }

                // Decode the data field (data starts 3 bytes after prologue)
                uint8_t decodedData[256];
                int dataStart = dataPos + 3;

                if (!decode62(diskImage.get(), 0, dataStart, decodedData)) {
                    std::cout << "ERROR: Checksum or decode failure for sector " << logical_sector << std::endl;
                    totalErrors++;
                    break;
                }

                // Compare with original
                int sectorErrors = 0;
                for (int i = 0; i < 256; i++) {
                    if (originalData[i] != decodedData[i]) {
                        if (sectorErrors < 5) {
                            printf("  Byte %d (0x%02X): expected 0x%02X, got 0x%02X\n",
                                   i, i, originalData[i], decodedData[i]);
                        }
                        sectorErrors++;
                    }
                }

                if (sectorErrors == 0) {
                    std::cout << "  PASS: All 256 bytes match!" << std::endl;
                } else {
                    std::cout << "  FAIL: " << sectorErrors << " byte(s) mismatch" << std::endl;
                    totalErrors += sectorErrors;
                }

                found = true;
                break;
            }

            // Continue searching from next position
            int trackSize = diskImage->getNibbleTrackSize(0);
            searchPos = (addrPos + 1) % trackSize;
        }

        if (!found) {
            std::cout << "ERROR: Could not find sector " << logical_sector << " in nibble track" << std::endl;
            totalErrors++;
        }
    }

    std::cout << std::endl;
    std::cout << "=== Summary ===" << std::endl;
    if (totalErrors == 0) {
        std::cout << "SUCCESS: All sectors encode/decode correctly!" << std::endl;
    } else {
        std::cout << "FAILED: " << totalErrors << " total error(s)" << std::endl;
    }

    return totalErrors;
}
