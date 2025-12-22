# DISK II Emulation Implementation Plan

## Overview

This document outlines the implementation plan for adding DISK II floppy disk emulation to the Apple IIe emulator, including support for DSK (DOS 3.3) disk image files.

---

## 1. Technical Background

### 1.1 DISK II Controller Hardware

The DISK II controller is remarkably simple - only 8 chips:
- **256-byte ROM** - Bootstrap code (loaded at $C600 for slot 6)
- **256-byte State Machine ROM** - Controls bit-to-byte conversion
- **74LS323** - 8-bit bidirectional shift register
- **74LS259** - Addressable latch (stepper motor phases)
- **74LS174** - Hex D flip-flop (state machine)
- **556** - Dual timer (motor shutoff delay)
- **74LS05** - Hex inverter
- **74LS132** - Quad NAND gate

### 1.2 Soft Switch Addresses (Slot 6: $C0E0-$C0EF)

| Address | Function | Description |
|---------|----------|-------------|
| $C0E0 | PH0_OFF | Stepper motor phase 0 off |
| $C0E1 | PH0_ON | Stepper motor phase 0 on |
| $C0E2 | PH1_OFF | Stepper motor phase 1 off |
| $C0E3 | PH1_ON | Stepper motor phase 1 on |
| $C0E4 | PH2_OFF | Stepper motor phase 2 off |
| $C0E5 | PH2_ON | Stepper motor phase 2 on |
| $C0E6 | PH3_OFF | Stepper motor phase 3 off |
| $C0E7 | PH3_ON | Stepper motor phase 3 on |
| $C0E8 | MOTOR_OFF | Drive motor off |
| $C0E9 | MOTOR_ON | Drive motor on |
| $C0EA | DRIVE1 | Select drive 1 |
| $C0EB | DRIVE2 | Select drive 2 |
| $C0EC | Q6L | Shift data (read mode) / Read data latch |
| $C0ED | Q6H | Load data (write mode) / Check write protect |
| $C0EE | Q7L | Read mode |
| $C0EF | Q7H | Write mode |

### 1.3 Stepper Motor Operation

The head position is controlled by a 4-phase stepper motor:
- Each phase corresponds to an electromagnet
- Activating phases in sequence (0→1→2→3→0...) moves the head inward
- Reverse sequence (3→2→1→0→3...) moves head outward
- **Two half-tracks = one full track** (35 tracks, but 70 half-track positions)
- Track 0 calibration: 80 half-steps toward track 0 (hitting mechanical stop)

**Phase Transition Logic:**
```
Current phases active → Next track position
Phase 0 → Phase 1: Move inward (toward track 34)
Phase 1 → Phase 0: Move outward (toward track 0)
```

### 1.4 Disk Format (DOS 3.3 / 16-sector)

| Parameter | Value |
|-----------|-------|
| Tracks | 35 |
| Sectors per track | 16 |
| Bytes per sector | 256 |
| Total capacity | 143,360 bytes (140 KB) |
| Rotation speed | ~300 RPM |
| Bit cell time | 4 microseconds |
| Bits per track | ~50,000 |
| Nibblized track size | ~6,656 bytes |

---

## 2. DSK File Format

### 2.1 File Structure

- **Size**: 143,360 bytes (35 tracks × 16 sectors × 256 bytes)
- **Organization**: Raw sector data in logical order
- **No headers or metadata** - just raw bytes

### 2.2 Sector Interleaving (DOS 3.3)

Physical-to-logical sector mapping for DOS 3.3 (.DO/.DSK):
```
Physical:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
Logical:   0  7 14  6 13  5 12  4 11  3 10  2  9  1  8 15
```

ProDOS (.PO) uses different interleaving:
```
Physical:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
Logical:   0  8  1  9  2 10  3 11  4 12  5 13  6 14  7 15
```

---

## 3. GCR Encoding (6-and-2)

### 3.1 Overview

The DISK II cannot write arbitrary bytes due to hardware constraints:
1. **MSB must be 1** (bit 7 = 1)
2. **No more than two consecutive 0 bits**

Solution: Encode 6 bits of data into 8 bits on disk (64 valid values).

### 3.2 Nibble Translation Table (6-bit → 8-bit)

```cpp
const uint8_t NIBBLE_ENCODE[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
```

### 3.3 Reverse Lookup Table (8-bit → 6-bit)

Generate a 256-byte table where `NIBBLE_DECODE[encoded] = original_6bit_value`.
Invalid nibbles map to 0xFF or a sentinel value.

### 3.4 Encoding Process (256 bytes → 342 encoded bytes)

1. **Split each byte into high 6 bits and low 2 bits**
2. **Pack low 2-bit pairs into first 86 bytes:**
   - Byte 0: bits from source[0], source[86], source[172] (2 bits each, reversed)
   - Byte 1: bits from source[1], source[87], source[173]
   - ... (bytes 84-85 only have 2 sources each)
3. **Store high 6 bits in remaining 256 bytes**
4. **XOR each value with previous** (running checksum)
5. **Encode through nibble table**

### 3.5 4-and-4 Encoding (Address Fields)

Used for volume, track, sector in address field:
```
Original byte: b7 b6 b5 b4 b3 b2 b1 b0
First nibble:  1  b7 1  b5 1  b3 1  b1
Second nibble: 1  b6 1  b4 1  b2 1  b0
```

---

## 4. On-Disk Sector Format

### 4.1 Complete Track Structure

```
[Gap 1: ~48 sync bytes (0xFF with 10-bit timing)]
[Sector 0]
[Gap 3: ~6-27 sync bytes]
[Sector 1]
...
[Sector 15]
[Gap 3]
(wraps around)
```

### 4.2 Single Sector Structure

```
=== ADDRESS FIELD ===
Sync bytes:     5+ × 0xFF (10-bit self-sync)
Prologue:       0xD5 0xAA 0x96
Volume:         2 bytes (4-and-4 encoded)
Track:          2 bytes (4-and-4 encoded)
Sector:         2 bytes (4-and-4 encoded)
Checksum:       2 bytes (4-and-4 encoded, XOR of vol/trk/sec)
Epilogue:       0xDE 0xAA 0xEB

=== GAP 2 ===
Sync bytes:     5-6 × 0xFF

=== DATA FIELD ===
Sync bytes:     5+ × 0xFF (10-bit self-sync)
Prologue:       0xD5 0xAA 0xAD
Data:           342 bytes (6-and-2 encoded, XOR chained)
Checksum:       1 byte (final XOR value, encoded)
Epilogue:       0xDE 0xAA 0xEB
```

### 4.3 Prologue/Epilogue Bytes

| Sequence | Purpose |
|----------|---------|
| D5 AA 96 | Address field prologue |
| D5 AA AD | Data field prologue |
| DE AA EB | Epilogue (both fields) |

These magic bytes never appear in encoded data.

---

## 5. Implementation Architecture

### 5.1 New Classes

```cpp
// include/emulator/disk_ii.hpp
class DiskII : public Device {
public:
    DiskII(int slot = 6);

    // Device interface
    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t value) override;
    std::pair<uint16_t, uint16_t> getAddressRange() const override;

    // Disk operations
    bool insertDisk(int drive, const std::string& filepath);
    void ejectDisk(int drive);
    bool isDiskInserted(int drive) const;
    bool isWriteProtected(int drive) const;
    void setWriteProtected(int drive, bool protect);

    // Emulation update (call every frame or periodically)
    void update(uint64_t cpuCycles);

    // State save/load
    void saveState(std::ostream& out) const;
    void loadState(std::istream& in);

private:
    // Hardware state
    int slot_;
    int selected_drive_;           // 0 or 1
    bool motor_on_;
    uint8_t phase_states_;         // Bits 0-3 = phases 0-3
    int current_half_track_;       // 0-69 (35 tracks × 2)

    // Controller state
    bool q6_;                      // Q6 latch (shift/load)
    bool q7_;                      // Q7 latch (read/write)
    uint8_t data_latch_;           // Data register

    // Disk state (per drive)
    struct Drive {
        bool disk_inserted = false;
        bool write_protected = true;
        std::vector<uint8_t> nibble_data;  // Nibblized track data
        size_t track_size = 0;              // Bytes in current track
        size_t bit_position = 0;            // Current read position
        uint64_t last_access_cycle = 0;
    };
    Drive drives_[2];

    // Timing
    uint64_t motor_off_cycle_;     // When motor will shut off
    static constexpr uint64_t MOTOR_OFF_DELAY = 1023000; // ~1 second

    // Disk rotation
    void advancePosition(uint64_t cycles);
    uint8_t readDataLatch();
    void writeDataLatch(uint8_t value);

    // Stepper motor
    void updatePhase(int phase, bool on);
    void moveHead();

    // Track loading
    void loadTrack(int drive, int track);
};

// include/emulator/disk_image.hpp
class DiskImage {
public:
    bool load(const std::string& filepath);
    bool save(const std::string& filepath);

    // Access raw sector data
    const uint8_t* getSector(int track, int sector) const;
    uint8_t* getSectorMutable(int track, int sector);

    // Get nibblized track data
    std::vector<uint8_t> getNibblizedTrack(int track) const;

    // Decode nibblized track back to sectors
    bool decodeTrack(int track, const std::vector<uint8_t>& nibbleData);

    bool isLoaded() const { return loaded_; }
    DiskFormat getFormat() const { return format_; }

private:
    bool loaded_ = false;
    DiskFormat format_ = DiskFormat::DOS33;
    std::array<uint8_t, 143360> data_;  // 35 tracks × 16 sectors × 256 bytes

    // Nibblization
    std::vector<uint8_t> nibblizeSector(int track, int sector, int volume) const;
    void encode6and2(const uint8_t* src, uint8_t* dest) const;
    void encode4and4(uint8_t value, uint8_t* dest) const;
};

enum class DiskFormat {
    DOS33,  // .dsk, .do - DOS 3.3 sector ordering
    PRODOS  // .po - ProDOS sector ordering
};
```

### 5.2 Integration with Existing Architecture

```cpp
// In emulator.hpp, add:
#include "disk_ii.hpp"

class Emulator {
    // ...existing members...
    std::unique_ptr<DiskII> disk_ii_;

    // In update():
    void update() {
        // ...existing code...
        disk_ii_->update(cpu_->getTotalCycles());
    }
};

// In mmu.cpp, replace the placeholder:
// Current code at lines 543-546:
if (address >= 0xC0E0 && address <= 0xC0EF) {
    return 0xFF;  // Empty slot
}

// Replace with:
if (address >= 0xC0E0 && address <= 0xC0EF) {
    return disk_ii_->read(address);
}
```

---

## 6. Detailed Implementation Steps

### Phase 1: Core Infrastructure

1. **Create DiskImage class**
   - Load/save DSK files (143,360 bytes)
   - Detect DOS 3.3 vs ProDOS sector ordering
   - Implement sector interleaving tables

2. **Create nibblization functions**
   - `encode6and2()` - Convert 256 bytes → 342 encoded bytes
   - `encode4and4()` - Encode address field values
   - `nibblizeSector()` - Create complete sector with headers/trailers
   - `nibblizeTrack()` - Create complete track with gaps

3. **Create denibblization functions** (for writes)
   - `decode6and2()` - Convert 342 bytes → 256 bytes
   - `decode4and4()` - Decode address field values
   - `findSector()` - Locate sector in nibble stream

### Phase 2: Controller Emulation

4. **Implement DiskII class**
   - Soft switch handlers for $C0E0-$C0EF
   - Phase/stepper motor state machine
   - Motor on/off with 1-second delay
   - Drive selection (drive 1/2)

5. **Implement disk rotation simulation**
   - Track current bit position
   - Advance position based on CPU cycles (4μs per bit)
   - Handle track wraparound (modulo track length)

6. **Implement read operations**
   - Return data from current position when Q6L accessed
   - Advance position after read
   - Handle MSB detection for valid nibbles

### Phase 3: Write Support

7. **Implement write operations**
   - Accept data when Q6H accessed in write mode
   - Write to nibble buffer at current position
   - Track modified sectors for write-back

8. **Implement write-back to disk image**
   - Decode modified nibble tracks
   - Update sector data in disk image
   - Save changes to file (or queue for save)

### Phase 4: Integration & ROM

9. **Load Disk II boot ROM**
   - Use existing `rom.loadDiskIIROM()` method
   - Place at $C600-$C6FF (slot 6 ROM space)
   - Handle INTCXROM soft switch for ROM visibility

10. **Integrate with MMU**
    - Route $C0E0-$C0EF to DiskII
    - Route $C600-$C6FF to Disk II ROM
    - Handle slot ROM enable/disable

11. **Integrate with Emulator**
    - Create DiskII in emulator constructor
    - Call DiskII::update() in main loop
    - Add to state save/load

### Phase 5: UI & Polish

12. **Add UI for disk management**
    - File dialog to insert/eject disks
    - Drive activity indicator (LED)
    - Write protect toggle

13. **Testing and debugging**
    - Test with DOS 3.3 system master
    - Test with various games/applications
    - Verify timing with copy-protected disks (optional)

---

## 7. Key Algorithms

### 7.1 Sector Nibblization

```cpp
std::vector<uint8_t> DiskImage::nibblizeSector(int track, int sector, int volume) const {
    std::vector<uint8_t> nibbles;
    nibbles.reserve(400);  // Approximate size

    const uint8_t* sectorData = getSector(track, sector);

    // Gap 2 (between sectors)
    for (int i = 0; i < 6; i++) nibbles.push_back(0xFF);

    // Address field
    nibbles.push_back(0xD5);
    nibbles.push_back(0xAA);
    nibbles.push_back(0x96);

    // Volume, track, sector (4-and-4 encoded)
    uint8_t encoded[2];
    encode4and4(volume, encoded);
    nibbles.push_back(encoded[0]);
    nibbles.push_back(encoded[1]);

    encode4and4(track, encoded);
    nibbles.push_back(encoded[0]);
    nibbles.push_back(encoded[1]);

    encode4and4(sector, encoded);
    nibbles.push_back(encoded[0]);
    nibbles.push_back(encoded[1]);

    encode4and4(volume ^ track ^ sector, encoded);  // Checksum
    nibbles.push_back(encoded[0]);
    nibbles.push_back(encoded[1]);

    nibbles.push_back(0xDE);
    nibbles.push_back(0xAA);
    nibbles.push_back(0xEB);

    // Gap 2
    for (int i = 0; i < 6; i++) nibbles.push_back(0xFF);

    // Data field
    nibbles.push_back(0xD5);
    nibbles.push_back(0xAA);
    nibbles.push_back(0xAD);

    // Encode sector data (6-and-2)
    uint8_t encodedData[343];  // 342 + checksum
    encode6and2(sectorData, encodedData);
    for (int i = 0; i < 343; i++) {
        nibbles.push_back(encodedData[i]);
    }

    nibbles.push_back(0xDE);
    nibbles.push_back(0xAA);
    nibbles.push_back(0xEB);

    return nibbles;
}
```

### 7.2 6-and-2 Encoding

```cpp
void DiskImage::encode6and2(const uint8_t* src, uint8_t* dest) const {
    uint8_t buffer[342];

    // Step 1: Pack low 2 bits into first 86 bytes
    for (int i = 0; i < 86; i++) {
        uint8_t val = 0;

        // Bit 0-1 from src[i]
        val |= ((src[i] & 0x01) << 1) | ((src[i] & 0x02) >> 1);

        // Bit 2-3 from src[i + 86]
        if (i + 86 < 256) {
            val |= ((src[i + 86] & 0x01) << 3) | ((src[i + 86] & 0x02) << 1);
        }

        // Bit 4-5 from src[i + 172]
        if (i + 172 < 256) {
            val |= ((src[i + 172] & 0x01) << 5) | ((src[i + 172] & 0x02) << 3);
        }

        buffer[85 - i] = val;  // Store in reverse order
    }

    // Step 2: Store high 6 bits
    for (int i = 0; i < 256; i++) {
        buffer[86 + i] = src[i] >> 2;
    }

    // Step 3: XOR chain and encode
    uint8_t prev = 0;
    for (int i = 0; i < 342; i++) {
        uint8_t val = buffer[i] ^ prev;
        dest[i] = NIBBLE_ENCODE[val & 0x3F];
        prev = buffer[i];
    }

    // Step 4: Final checksum
    dest[342] = NIBBLE_ENCODE[prev & 0x3F];
}
```

### 7.3 Stepper Motor State Machine

```cpp
void DiskII::updatePhase(int phase, bool on) {
    if (on) {
        phase_states_ |= (1 << phase);
    } else {
        phase_states_ &= ~(1 << phase);
    }
    moveHead();
}

void DiskII::moveHead() {
    // Current phase = half_track mod 4
    int current_phase = (current_half_track_ / 2) % 4;

    // Find the active phase
    int active_phase = -1;
    for (int i = 0; i < 4; i++) {
        if (phase_states_ & (1 << i)) {
            active_phase = i;
            break;  // Take first active phase
        }
    }

    if (active_phase < 0) return;  // No phase active

    // Calculate phase delta
    int delta = active_phase - current_phase;

    // Handle wraparound (-3 means +1, +3 means -1)
    if (delta == 3) delta = -1;
    if (delta == -3) delta = 1;

    // Move half-track
    if (delta == 1 && current_half_track_ < 69) {
        current_half_track_++;
        loadTrack(selected_drive_, current_half_track_ / 2);
    } else if (delta == -1 && current_half_track_ > 0) {
        current_half_track_--;
        loadTrack(selected_drive_, current_half_track_ / 2);
    }
}
```

### 7.4 Disk Rotation Simulation

```cpp
void DiskII::advancePosition(uint64_t cycles) {
    if (!motor_on_) return;

    Drive& drive = drives_[selected_drive_];
    if (!drive.disk_inserted || drive.track_size == 0) return;

    // Calculate bits elapsed since last access
    // 1.023 MHz CPU, 4 cycles per bit = ~255,750 bits/second
    // At 300 RPM, one rotation = 200ms = ~51,150 bits
    uint64_t cycle_delta = cycles - drive.last_access_cycle;
    size_t bits_elapsed = cycle_delta / 4;  // 4 CPU cycles per bit

    // Advance position with wraparound
    drive.bit_position = (drive.bit_position + bits_elapsed) % (drive.track_size * 8);
    drive.last_access_cycle = cycles;
}

uint8_t DiskII::readDataLatch() {
    Drive& drive = drives_[selected_drive_];

    // Get current byte position
    size_t byte_pos = drive.bit_position / 8;

    // Return the nibble at current position
    if (byte_pos < drive.nibble_data.size()) {
        // Advance to next byte
        drive.bit_position = ((byte_pos + 1) * 8) % (drive.track_size * 8);
        return drive.nibble_data[byte_pos];
    }

    return 0x00;  // No disk
}
```

---

## 8. Timing Considerations

### 8.1 Critical Timings

| Parameter | Value | Notes |
|-----------|-------|-------|
| Bit cell | 4 μs | 4 CPU cycles at 1.023 MHz |
| Byte read | 32 μs | 8 bits × 4 μs |
| Track rotation | ~200 ms | 300 RPM |
| Motor off delay | 1 second | Timer on controller card |
| Stepper settle | ~20 ms | Per half-track movement |

### 8.2 Accuracy Requirements

For basic DOS 3.3 disk support, timing accuracy is not critical:
- The emulated CPU is cycle-accurate
- Disk position advances based on CPU cycles
- Most software is tolerant of timing variations

For copy-protected disks, more accurate timing may be needed:
- Bit-level position tracking
- Exact sync byte timing
- Quarter-track support

---

## 9. ROM Requirements

### 9.1 Disk II Boot ROM (P5A ROM)

- **Size**: 256 bytes
- **Location**: $C600-$C6FF (slot 6)
- **File**: Typically named "DISK2.rom" or "P5A.rom"
- **Purpose**: Bootstrap code to load DOS from disk

The ROM is visible when:
1. INTCXROM is OFF (slot ROMs enabled)
2. Accessing $C600-$C6FF

### 9.2 ROM Loading

```cpp
// In rom.cpp
bool ROM::loadDiskIIROM(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> data(256);
    file.read(reinterpret_cast<char*>(data.data()), 256);

    // Copy to expansion ROM area at slot 6 offset ($600)
    std::copy(data.begin(), data.end(), expansion_rom_.begin() + 0x500);

    return true;
}
```

---

## 10. Testing Strategy

### 10.1 Unit Tests

1. **Nibblization tests**
   - Encode/decode round-trip
   - Known sector data verification
   - Checksum validation

2. **Stepper motor tests**
   - Phase sequence verification
   - Track boundary handling
   - Half-track positioning

3. **Controller tests**
   - Soft switch read/write
   - Mode transitions (read/write)
   - Motor timing

### 10.2 Integration Tests

1. **Boot tests**
   - DOS 3.3 system master boot
   - ProDOS boot
   - Blank disk formatting

2. **File operations**
   - CATALOG command
   - LOAD/SAVE programs
   - BLOAD/BSAVE binary files

3. **Game compatibility**
   - Standard games (non-protected)
   - Common protected titles (if timing is accurate)

---

## 11. Future Enhancements

1. **WOZ file format support** - Bit-accurate disk images
2. **NIB file format support** - Pre-nibblized images
3. **Write support** - Saving changes to disk images
4. **Dual drive support** - Drive 1 and Drive 2
5. **Drive sounds** - Motor, head movement audio
6. **Copy protection** - Quarter-track, timing-sensitive schemes
7. **3.5" disk support** - For Apple IIc/IIgs compatibility

---

## 12. References

### Technical Documentation
- [Apple GCR Disk Encoding](https://github.com/TomHarte/CLK/wiki/Apple-GCR-disk-encoding) - Comprehensive GCR encoding details
- [WOZ Disk Image Reference](https://applesaucefdc.com/woz/reference1/) - Disk II controller and timing
- [The Amazing Disk II Controller Card](https://www.bigmessowires.com/2021/11/12/the-amazing-disk-ii-controller-card/) - Hardware design analysis
- [Apple II Copy Protection](https://www.bigmessowires.com/2015/08/27/apple-ii-copy-protection/) - Sector format details

### Source Code References
- [AppleWin Disk.cpp](https://github.com/AppleWin/AppleWin/blob/master/source/Disk.cpp) - Mature C++ implementation
- [FruitMachine-Swift DiskImage.swift](https://github.com/Luigi30/FruitMachine-Swift/blob/master/FruitMachine/AppleII/Peripherals/DiskII/DiskImage.swift) - Clean Swift implementation
- [FPGA Disk Controller](https://github.com/steve-chamberlin/fpga-disk-controller/blob/master/disk-II-boot-process.txt) - Boot process documentation

### Books
- "Beneath Apple DOS" - Comprehensive DOS and disk format reference
- "Understanding the Apple II" by Jim Sather - Hardware details
