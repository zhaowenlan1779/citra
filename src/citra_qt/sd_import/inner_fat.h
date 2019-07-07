// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

union TableOffset {
    // This has different meanings for different savegame layouts
    struct { // duplicate data = true
        u32_le block_index;
        u32_le block_count;
    } duplicate;

    u64_le non_duplicate; // duplicate data = false
};

struct FATHeader {
    u32_le magic;
    u32_le version;
    u64_le filesystem_information_offset;
    u64_le image_size;
    u32_le image_block_size;
    INSERT_PADDING_BYTES(8);
    u32_le data_region_block_size;
    u64_le directory_hash_table_offset;
    u32_le directory_hash_table_bucket_count;
    INSERT_PADDING_BYTES(4);
    u64_le file_hash_table_offset;
    u32_le file_hash_table_bucket_count;
    INSERT_PADDING_BYTES(4);
    u64_le file_allocation_table_offset;
    u32_le file_allocation_table_entry_count;
    INSERT_PADDING_BYTES(4);
    u64_le data_region_offset;
    u32_le data_region_block_count;
    INSERT_PADDING_BYTES(4);
    TableOffset directory_entry_table;
    u32_le maximum_directory_count;
    INSERT_PADDING_BYTES(4);
    TableOffset file_entry_table;
    u32_le maximum_file_count;
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(FATHeader) == 0x88, "FATHeader has incorrect size");

struct DirectoryEntryTableEntry {
    u32_le parent_directory_index;
    std::array<char, 16> name;
    u32_le next_sibling_index;
    u32_le first_subdirectory_index;
    u32_le first_file_index;
    INSERT_PADDING_BYTES(4);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(DirectoryEntryTableEntry) == 0x28,
              "DirectoryEntryTableEntry has incorrect size");

struct FileEntryTableEntry {
    u32_le parent_directory_index;
    std::array<char, 16> name;
    u32_le next_sibling_index;
    INSERT_PADDING_BYTES(4);
    u32_le data_block_index;
    u64_le file_size;
    INSERT_PADDING_BYTES(4);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(FileEntryTableEntry) == 0x30, "FileEntryTableEntry has incorrect size");

struct FATNode {
    union {
        BitField<0, 31, u32> index;
        BitField<31, 1, u32> flag;

        u32_le raw;
    } u, v;
};

/**
 * Class for the inner FAT filesystem of SD Savegames.
 * There exist two different layouts of SD Savegames. This class handles both of them, with
 * different constructors.
 */
class InnerFAT {
public:
    /// Initializes the filesystem (layout: duplicate data = true)
    explicit InnerFAT(const std::vector<u8>& data);

    /// Initializes the filesystem (layout: duplicate data = false)
    explicit InnerFAT(const std::vector<u8>& partitionA, std::vector<u8> partitionB);

    /**
     * Extracts everything in the file system to a certain path.
     * @return true on success, false otherwise
     */
    bool ExtractAll(const std::string& path) const;

    /**
     * Writes the corresponding archive metadata to a certain path.
     * @return true on success, false otherwise
     */
    bool WriteMetadata(const std::string& path) const;

private:
    /**
     * Extracts the index-th file in the file entry table to a certain path. (The path does not
     * contain the file name).
     * @return true on success, false otherwise
     */
    bool ExtractFile(const std::string& path, std::size_t index) const;

    /**
     * Extracts all files in the index-th directory in the directory entry table to a certain path.
     * @return true on success, false otherwise
     */
    bool ExtractDirectory(const std::string& path, std::size_t index) const;

    FATHeader header;
    std::vector<u8> data_region;
    std::vector<DirectoryEntryTableEntry> directory_entry_table;
    std::vector<FileEntryTableEntry> file_entry_table;
    std::vector<FATNode> fat;
    bool duplicate_data;
};
