// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

#pragma pack(push, 1)
struct DataDescriptor {
    u64_le offset;
    u64_le size;
};

struct DISAHeader {
    u32_le magic;
    u32_le version;
    u32_le partition_count;
    INSERT_PADDING_BYTES(4);
    u64_le secondary_partition_table_offset;
    u64_le primary_partition_table_offset;
    u64_le partition_table_size;
    std::array<DataDescriptor, 2> partition_descriptors;
    std::array<DataDescriptor, 2> partitions;
    u8 active_partition_table;
    INSERT_PADDING_BYTES(3);
    std::array<u8, 0x20> sha_hash;
    INSERT_PADDING_BYTES(0x74);
};
static_assert(sizeof(DISAHeader) == 0x100, "Size of DISA header is incorrect");

struct DIFIHeader {
    u32_le magic;
    u32_le version;
    DataDescriptor ivfc;
    DataDescriptor dpfs;
    DataDescriptor partition_hash;
    u8 enable_external_IVFC_level_4;
    u8 dpfs_level1_selector;
    INSERT_PADDING_BYTES(2);
    u64_le external_IVFC_level_4_offset;
};
static_assert(sizeof(DIFIHeader) == 0x44, "Size of DIFI header is incorrect");

/// Descriptor for both IVFC and DPFS levels
struct LevelDescriptor {
    u64_le offset;
    u64_le size;
    u32_le block_size; // In log2
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(LevelDescriptor) == 0x18, "Size of level descriptor is incorrect");

struct IVFCDescriptor {
    u32_le magic;
    u32_le version;
    u64_le master_hash_size;
    std::array<LevelDescriptor, 4> levels;
    u64_le descriptor_size;
};
static_assert(sizeof(IVFCDescriptor) == 0x78, "Size of IVFC descriptor is incorrect");

struct DPFSDescriptor {
    u32_le magic;
    u32_le version;
    std::array<LevelDescriptor, 3> levels;
};
static_assert(sizeof(DPFSDescriptor) == 0x50, "Size of DPFS descriptor is incorrect");
#pragma pack(pop)

class DPFSContainer {
public:
    explicit DPFSContainer(DPFSDescriptor descriptor, u8 level1_selector, std::vector<u32_le> data);

    /// Unwraps the DPFS Tree, returning actual data in Level3.
    std::vector<u8> GetLevel3Data() const;

private:
    u8 GetBit(u8 level, u8 selector, u64 index) const;
    u8 GetByte(u8 level, u8 selector, u64 index) const;

    DPFSDescriptor descriptor;
    u8 level1_selector;
    std::vector<u32_le> data;
};

class DISAContainer {
public:
    explicit DISAContainer(std::vector<u8> data);

    /// Unwraps the whole container, returning the data in IVFC Level 4 of all partitions.
    std::vector<std::vector<u8>> GetIVFCLevel4Data() const;

private:
    /// Unwraps the whole container, returning the data in IVFC Level 4 of a partition.
    std::vector<u8> GetPartitionData(u8 index) const;

    std::vector<u8> data;
    DISAHeader header;
    u64_le partition_table_offset;
};
