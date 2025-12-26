/**
 * catalog_reader - Test app to read DOS 3.3 catalog from a DSK image
 *
 * This tests the disk_image class by directly reading sector data
 * and parsing the DOS 3.3 VTOC and catalog structures.
 *
 * DOS 3.3 Disk Structure:
 * - VTOC (Volume Table of Contents) at Track 17, Sector 0
 * - Catalog chain starts at track/sector pointed to by VTOC bytes 1-2
 * - Each catalog sector contains up to 7 file entries
 */

#include "emulator/disk_image.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>

// DOS 3.3 constants
constexpr int VTOC_TRACK = 17;
constexpr int VTOC_SECTOR = 0;
constexpr int CATALOG_ENTRY_SIZE = 35;
constexpr int CATALOG_ENTRIES_PER_SECTOR = 7;
constexpr int CATALOG_FIRST_ENTRY_OFFSET = 0x0B;

// File type names
const char* getFileTypeName(uint8_t type_byte)
{
    bool locked = (type_byte & 0x80) != 0;
    uint8_t file_type = type_byte & 0x7F;

    const char* type_name;
    switch (file_type)
    {
    case 0x00:
        type_name = "T";
        break; // Text
    case 0x01:
        type_name = "I";
        break; // Integer BASIC
    case 0x02:
        type_name = "A";
        break; // Applesoft BASIC
    case 0x04:
        type_name = "B";
        break; // Binary
    case 0x08:
        type_name = "S";
        break; // S-type (special)
    case 0x10:
        type_name = "R";
        break; // Relocatable
    case 0x20:
        type_name = "a";
        break; // new A
    case 0x40:
        type_name = "b";
        break; // new B
    default:
        type_name = "?";
        break;
    }

    static char result[4];
    snprintf(result, sizeof(result), "%c%s", locked ? '*' : ' ', type_name);
    return result;
}

// Convert Apple II high-ASCII filename to normal string
std::string convertFilename(const uint8_t* data, int length)
{
    std::string result;
    result.reserve(length);

    for (int i = 0; i < length; i++)
    {
        // Clear high bit and convert to normal ASCII
        char c = data[i] & 0x7F;
        // Replace non-printable with space
        if (c < 0x20 || c > 0x7E)
        {
            c = ' ';
        }
        result += c;
    }

    // Trim trailing spaces
    while (!result.empty() && result.back() == ' ')
    {
        result.pop_back();
    }

    return result;
}

void printVTOC(const uint8_t* vtoc)
{
    std::cout << "\n=== VTOC (Volume Table of Contents) ===" << std::endl;
    std::cout << "Catalog Track:    " << (int)vtoc[1] << std::endl;
    std::cout << "Catalog Sector:   " << (int)vtoc[2] << std::endl;
    std::cout << "DOS Version:      " << (int)vtoc[3] << std::endl;
    std::cout << "Volume Number:    " << (int)vtoc[6] << std::endl;
    std::cout << "Max T/S Pairs:    " << (int)vtoc[0x27] << std::endl;
    std::cout << "Last Track Alloc: " << (int)vtoc[0x30] << std::endl;
    std::cout << "Direction:        " << ((int)vtoc[0x31] == 1 ? "+1" : "-1") << std::endl;
    std::cout << "Tracks Per Disk:  " << (int)vtoc[0x34] << std::endl;
    std::cout << "Sectors Per Trk:  " << (int)vtoc[0x35] << std::endl;
    std::cout << "Bytes Per Sector: " << (vtoc[0x36] | (vtoc[0x37] << 8)) << std::endl;
}

void printCatalogHeader()
{
    std::cout << "\n=== CATALOG ===" << std::endl;
    std::cout << std::setw(3) << "LK" << " "
              << std::setw(2) << "T" << " "
              << std::setw(30) << std::left << "FILENAME" << std::right << " "
              << std::setw(5) << "SECTS" << " "
              << std::setw(5) << "T/S" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
}

int printCatalogEntry(const uint8_t* entry, int entry_num)
{
    uint8_t first_ts_track = entry[0];
    uint8_t first_ts_sector = entry[1];
    uint8_t file_type = entry[2];

    // Skip deleted or empty entries
    if (first_ts_track == 0x00)
    {
        return 0; // Deleted entry
    }
    if (first_ts_track == 0xFF)
    {
        return -1; // Never used - end of catalog
    }

    // Get filename (30 bytes starting at offset 3)
    std::string filename = convertFilename(&entry[3], 30);

    // Get sector count (2 bytes at offset 33-34, little-endian)
    int sector_count = entry[33] | (entry[34] << 8);

    // Print entry
    std::cout << std::setw(3) << getFileTypeName(file_type) << " "
              << std::setw(2) << (int)first_ts_track << " "
              << std::setw(30) << std::left << filename << std::right << " "
              << std::setw(5) << sector_count << " "
              << std::setw(2) << (int)first_ts_track << ","
              << std::setw(2) << (int)first_ts_sector << std::endl;

    return 1;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <disk_image.dsk>" << std::endl;
        std::cerr << "\nReads and displays the DOS 3.3 catalog from a DSK image." << std::endl;
        return 1;
    }

    const char* disk_path = argv[1];

    // Load the disk image
    DiskImage disk;
    if (!disk.load(disk_path))
    {
        std::cerr << "Failed to load disk image: " << disk_path << std::endl;
        return 1;
    }

    std::cout << "\nDisk image loaded: " << disk_path << std::endl;
    std::cout << "Format: " << (disk.getFormat() == DiskImage::Format::DSK ? "DSK" : "WOZ2") << std::endl;

    // For DSK format, we can directly access sector data
    if (disk.getFormat() != DiskImage::Format::DSK)
    {
        std::cerr << "\nNote: Direct sector access only works with DSK format." << std::endl;
        std::cerr << "WOZ2 format stores raw bitstream data, not sector data." << std::endl;
        return 1;
    }

    // Read VTOC (Track 17, Sector 0)
    // We need to access raw_data_ directly, but getSectorData is private
    // Let's add a public method or work around this

    // Actually, looking at the class, getSectorData is private. We need to either:
    // 1. Make it public
    // 2. Add a public readSector method
    // 3. Access raw_data_ directly (not ideal)

    // For now, let's read the raw file directly as a workaround
    std::ifstream file(disk_path, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open disk image for raw read" << std::endl;
        return 1;
    }

    // Read entire DSK file
    std::vector<uint8_t> raw_data(DiskImage::DSK_IMAGE_SIZE);
    file.read(reinterpret_cast<char*>(raw_data.data()), DiskImage::DSK_IMAGE_SIZE);
    file.close();

    // Helper lambda to get sector data
    auto getSector = [&raw_data](int track, int sector) -> const uint8_t* {
        if (track < 0 || track >= DiskImage::TRACKS ||
            sector < 0 || sector >= DiskImage::SECTORS_PER_TRACK)
        {
            return nullptr;
        }
        int offset = (track * DiskImage::SECTORS_PER_TRACK + sector) * DiskImage::BYTES_PER_SECTOR;
        return &raw_data[offset];
    };

    // Read VTOC
    const uint8_t* vtoc = getSector(VTOC_TRACK, VTOC_SECTOR);
    if (!vtoc)
    {
        std::cerr << "Failed to read VTOC" << std::endl;
        return 1;
    }

    // Print VTOC info
    printVTOC(vtoc);

    // Get catalog start location
    int cat_track = vtoc[1];
    int cat_sector = vtoc[2];

    if (cat_track == 0)
    {
        std::cerr << "\nNo catalog found (empty disk)" << std::endl;
        return 0;
    }

    // Print catalog header
    printCatalogHeader();

    int total_files = 0;
    int catalog_sectors_read = 0;
    const int MAX_CATALOG_SECTORS = 20; // Prevent infinite loop

    // Follow the catalog chain
    while (cat_track != 0 && catalog_sectors_read < MAX_CATALOG_SECTORS)
    {
        const uint8_t* cat_sector_data = getSector(cat_track, cat_sector);
        if (!cat_sector_data)
        {
            std::cerr << "Failed to read catalog sector T" << cat_track << " S" << cat_sector << std::endl;
            break;
        }

        catalog_sectors_read++;

        // Process entries in this catalog sector
        for (int i = 0; i < CATALOG_ENTRIES_PER_SECTOR; i++)
        {
            const uint8_t* entry = &cat_sector_data[CATALOG_FIRST_ENTRY_OFFSET + i * CATALOG_ENTRY_SIZE];
            int result = printCatalogEntry(entry, i);
            if (result > 0)
            {
                total_files++;
            }
            else if (result < 0)
            {
                // End of catalog
                goto done;
            }
        }

        // Move to next catalog sector
        cat_track = cat_sector_data[1];
        cat_sector = cat_sector_data[2];
    }

done:
    std::cout << std::string(50, '-') << std::endl;
    std::cout << "Total files: " << total_files << std::endl;
    std::cout << "Catalog sectors read: " << catalog_sectors_read << std::endl;

    return 0;
}
