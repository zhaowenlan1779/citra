// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include "citra_qt/sd_import/disa_container.h"
#include "common/assert.h"

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

DPFSContainer::DPFSContainer(DPFSDescriptor descriptor_, u8 level1_selector_,
                             std::vector<u32_le> data_)
    : descriptor(descriptor_), level1_selector(level1_selector_), data(std::move(data_)) {

    ASSERT_MSG(descriptor.magic == MakeMagic('D', 'P', 'F', 'S'), "DPFS Magic is not correct");
    ASSERT_MSG(descriptor.version == 0x10000, "DPFS Version is not correct");
}

u8 DPFSContainer::GetBit(u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");
    return (data[(descriptor.levels[level].offset + selector * descriptor.levels[level].size) / 4 +
                 index / 32] >>
            (31 - (index % 32))) &
           1;
}

u8 DPFSContainer::GetByte(u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");
    return reinterpret_cast<const u8*>(
        data.data())[descriptor.levels[level].offset + selector * descriptor.levels[level].size +
                     index];
}

std::vector<u8> DPFSContainer::GetLevel3Data() const {
    std::vector<u8> level3_data(descriptor.levels[2].size);
    for (std::size_t i = 0; i < level3_data.size(); i++) {
        auto level2_bit_index = i / std::pow(2, descriptor.levels[1].block_size);
        auto level1_bit_index =
            (level2_bit_index / 8) / std::pow(2, descriptor.levels[0].block_size);
        auto level2_selector = GetBit(0, level1_selector, level1_bit_index);
        auto level3_selector = GetBit(1, level2_selector, level2_bit_index);
        level3_data[i] = GetByte(2, level3_selector, i);
    }
    return level3_data;
}

DISAContainer::DISAContainer(std::vector<u8> data_) : data(std::move(data_)) {
    ASSERT_MSG(data.size() >= 0x200, "DISA size is too small");
    std::memcpy(&header, data.data() + 0x100, 0x100);
    ASSERT_MSG(header.magic == MakeMagic('D', 'I', 'S', 'A'), "DISA Magic is not correct");
    ASSERT_MSG(header.version == 0x40000, "DISA Version is not correct");

    if (header.active_partition_table == 0) { // primary
        partition_table_offset = header.primary_partition_table_offset;
    } else {
        partition_table_offset = header.secondary_partition_table_offset;
    }
}

std::vector<u8> DISAContainer::GetPartitionData(u8 index) const {
    ASSERT_MSG(header.partition_table_size >= sizeof(DIFIHeader),
               "Partition table size is too small");

    auto partition_descriptor_offset =
        partition_table_offset + header.partition_descriptors[index].offset;

    DIFIHeader difi;
    std::memcpy(&difi, data.data() + partition_descriptor_offset, sizeof(difi));
    ASSERT_MSG(difi.magic == MakeMagic('D', 'I', 'F', 'I'), "DIFI Magic is not correct");
    ASSERT_MSG(difi.version == 0x10000, "DIFI Version is not correct");

    ASSERT_MSG(difi.ivfc.size >= sizeof(IVFCDescriptor), "IVFC descriptor size is too small");
    IVFCDescriptor ivfc_descriptor;
    std::memcpy(&ivfc_descriptor, data.data() + partition_descriptor_offset + difi.ivfc.offset,
                sizeof(ivfc_descriptor));

    if (difi.enable_external_IVFC_level_4) {
        std::vector<u8> result(
            data.data() + header.partitions[index].offset + difi.external_IVFC_level_4_offset,
            data.data() + header.partitions[index].offset + difi.external_IVFC_level_4_offset +
                ivfc_descriptor.levels[3].size);
        return result;
    }

    // Unwrap DPFS Tree
    ASSERT_MSG(difi.dpfs.size >= sizeof(DPFSDescriptor), "DPFS descriptor size is too small");
    DPFSDescriptor dpfs_descriptor;
    std::memcpy(&dpfs_descriptor, data.data() + partition_descriptor_offset + difi.dpfs.offset,
                sizeof(dpfs_descriptor));

    std::vector<u32> partition_data(header.partitions[index].size / 4);
    std::memcpy(partition_data.data(), data.data() + header.partitions[index].offset,
                header.partitions[index].size);

    DPFSContainer dpfs_container(dpfs_descriptor, difi.dpfs_level1_selector, partition_data);
    auto ivfc_data = dpfs_container.GetLevel3Data();

    std::vector<u8> result(ivfc_data.data() + ivfc_descriptor.levels[3].offset,
                           ivfc_data.data() + ivfc_descriptor.levels[3].offset +
                               ivfc_descriptor.levels[3].size);
    return result;
}

std::vector<std::vector<u8>> DISAContainer::GetIVFCLevel4Data() const {
    if (header.partition_count == 1) {
        return {GetPartitionData(0)};
    } else {
        return {GetPartitionData(0), GetPartitionData(1)};
    }
}
