// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"

class SDMCDecryptor {
public:
    /**
     * Initializes the importer.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
     */
    explicit SDMCDecryptor(const std::string& root_folder);

    /**
     * Decrypts a file from the SD card and writes it into another file.
     * @param source Path to the file relative to the root folder, starting with "/".
     * @param destination Path to the destination file.
     */
    void DecryptAndWriteFile(const std::string& source, const std::string& destination);

    /**
     * Decrypts a file and reads it into a vector.
     * @param source Path to the file relative to the root folder, starting with "/".
     */
    std::vector<u8> DecryptFile(const std::string& source);

private:
    std::string root_folder;
};
