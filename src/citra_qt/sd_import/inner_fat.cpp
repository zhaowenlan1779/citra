// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "citra_qt/sd_import/inner_fat.h"
#include "common/assert.h"
#include "common/file_util.h"
#include "core/file_sys/archive_backend.h"

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

InnerFAT::InnerFAT(const std::vector<u8>& data) : duplicate_data(true) {
    ASSERT_MSG(data.size() >= sizeof(FATHeader), "Data size is too small");

    std::memcpy(&header, data.data(), sizeof(header));
    ASSERT_MSG(header.magic == MakeMagic('S', 'A', 'V', 'E'), "Inner FAT magic is incorrect");
    ASSERT_MSG(header.version == 0x40000, "Inner FAT version is incorrect");

    // Read data region
    data_region.resize(header.data_region_block_count * header.data_region_block_size);
    std::memcpy(data_region.data(), data.data() + header.data_region_offset, data_region.size());

    // Read directory & file entry tables.
    // Note: Directory & file entry tables are allocated in the data region as if they are two
    // normal files. However, only continuous allocation has been observed, so directly reading
    // the bytes should be safe.
    directory_entry_table.resize(header.maximum_directory_count + 2); // including head and root
    std::memcpy(directory_entry_table.data(),
                data.data() + header.data_region_offset +
                    header.directory_entry_table.duplicate.block_index *
                        header.data_region_block_size,
                directory_entry_table.size() * sizeof(DirectoryEntryTableEntry));

    file_entry_table.resize(header.maximum_file_count + 1); // including head
    std::memcpy(file_entry_table.data(),
                data.data() + header.data_region_offset +
                    header.file_entry_table.duplicate.block_index * header.data_region_block_size,
                file_entry_table.size() * sizeof(FileEntryTableEntry));

    // Read file allocation table
    fat.resize(header.file_allocation_table_entry_count);
    std::memcpy(fat.data(), data.data() + header.file_allocation_table_offset,
                fat.size() * sizeof(FATNode));
}

InnerFAT::InnerFAT(const std::vector<u8>& partitionA, std::vector<u8> partitionB)
    : data_region(std::move(partitionB)), duplicate_data(false) {

    ASSERT_MSG(partitionA.size() >= sizeof(FATHeader), "Size of partition A is too small");

    std::memcpy(&header, partitionA.data(), sizeof(header));
    ASSERT_MSG(header.magic == MakeMagic('S', 'A', 'V', 'E'), "Inner FAT magic is incorrect");
    ASSERT_MSG(header.version == 0x40000, "Inner FAT version is incorrect");

    // Read directory & file entry tables
    directory_entry_table.resize(header.maximum_directory_count + 2); // including head and root
    std::memcpy(directory_entry_table.data(),
                partitionA.data() + header.directory_entry_table.non_duplicate,
                directory_entry_table.size() * sizeof(DirectoryEntryTableEntry));

    file_entry_table.resize(header.maximum_file_count + 1); // including head
    std::memcpy(file_entry_table.data(), partitionA.data() + header.file_entry_table.non_duplicate,
                file_entry_table.size() * sizeof(FileEntryTableEntry));

    // Read file allocation table
    fat.resize(header.file_allocation_table_entry_count);
    std::memcpy(fat.data(), partitionA.data() + header.file_allocation_table_offset,
                fat.size() * sizeof(FATNode));
}

bool InnerFAT::ExtractFile(const std::string& path, std::size_t index) const {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Frontend, "Could not create path {}", path);
        return false;
    }

    auto entry = file_entry_table[index];

    std::array<char, 17> name_data = {}; // Append a null terminator
    std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

    std::string name{name_data.data()};
    FileUtil::IOFile file(path + name, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Frontend, "Could not open file {}", path + name);
        return false;
    }

    u32 block = entry.data_block_index;
    if (block == 0x80000000) { // empty file
        return true;
    }

    while (true) {
        // Entry index is block index + 1
        auto block_data = fat[block + 1];

        u32 last_block = block;
        if (block_data.v.flag) { // This node has multiple entries
            last_block = fat[block + 2].v.index - 1;
        }

        std::size_t size = header.data_region_block_size * (last_block - block + 1);
        if (file.WriteBytes(data_region.data() + header.data_region_block_size * block, size) !=
            size) {
            LOG_ERROR(Frontend, "Write data failed (file: {})", path + name);
            return false;
        }

        if (block_data.v.index == 0) // last node
            break;

        block = block_data.v.index - 1;
    }

    return true;
}

bool InnerFAT::ExtractDirectory(const std::string& path, std::size_t index) const {
    auto entry = directory_entry_table[index];

    std::array<char, 17> name_data = {}; // Append a null terminator
    std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

    std::string name = name_data.data();
    std::string new_path = name.empty() ? path : path + name + "/"; // Name is empty for root

    // Files
    u32 cur = entry.first_file_index;
    while (cur != 0) {
        if (!ExtractFile(new_path, cur))
            return false;
        cur = file_entry_table[cur].next_sibling_index;
    }

    // Subdirectories
    cur = entry.first_subdirectory_index;
    while (cur != 0) {
        if (!ExtractDirectory(new_path, cur))
            return false;
        cur = directory_entry_table[cur].next_sibling_index;
    }

    return true;
}

bool InnerFAT::ExtractAll(const std::string& path) const {
    return ExtractDirectory(path, 1); // 1 = root
}

bool InnerFAT::WriteMetadata(const std::string& path) const {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Frontend, "Could not create path {}", path);
        return false;
    }

    // Tests on a physical 3DS shows that the `total_size` field seems to always be 0,
    // at least when requested with the UserSaveData archive. More investigation is required.
    FileSys::ArchiveFormatInfo format_info = {
        /* total_size */ 0x0,
        /* number_directories */ header.maximum_directory_count,
        /* number_files */ header.maximum_file_count,
        /* duplicate_data */ duplicate_data};

    FileUtil::IOFile file(path, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Frontend, "Could not open file {}", path);
        return false;
    }
    if (file.WriteBytes(&format_info, sizeof(format_info)) != sizeof(format_info)) {
        LOG_ERROR(Frontend, "Write data failed (file: {})", path);
        return false;
    }
    return true;
}
