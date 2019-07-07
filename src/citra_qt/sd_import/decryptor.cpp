// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <vector>
#include <cryptopp/aes.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "citra_qt/sd_import/decryptor.h"
#include "common/assert.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/hw/aes/key.h"

SDMCDecryptor::SDMCDecryptor(const std::string& root_folder_) : root_folder(root_folder_) {
    ASSERT_MSG(HW::AES::IsNormalKeyAvailable(HW::AES::SDKey),
               "SD Key must be available in order to decrypt");

    if (root_folder.back() == '/' || root_folder.back() == '\\') {
        // Remove '/' or '\' character at the end as we will add them back when combining path
        root_folder.erase(root_folder.size() - 1);
    }
}

inline std::array<u8, 16> GetFileCTR(const std::string& path) {
    auto path_utf16 = Common::UTF8ToUTF16(path);
    std::vector<u8> path_data(path_utf16.size() * 2 + 2, 0); // Add the '\0' character
    std::memcpy(path_data.data(), path_utf16.data(), path_utf16.size() * 2);

    CryptoPP::SHA256 sha;
    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> hash;
    sha.CalculateDigest(hash.data(), path_data.data(), path_data.size());

    std::array<u8, 16> ctr;
    for (int i = 0; i < 16; i++) {
        ctr[i] = hash[i] ^ hash[16 + i];
    }
    return ctr;
}

void SDMCDecryptor::DecryptAndWriteFile(const std::string& source, const std::string& destination) {

    auto ctr = GetFileCTR(source);
    auto key = HW::AES::GetNormalKey(HW::AES::SDKey);
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    aes.SetKeyWithIV(key.data(), key.size(), ctr.data());

    std::string absolute_source = root_folder + source;
    CryptoPP::FileSource(absolute_source.c_str(), true,
                         new CryptoPP::StreamTransformationFilter(
                             aes, new CryptoPP::FileSink(destination.c_str(), true)),
                         true);
}

std::vector<u8> SDMCDecryptor::DecryptFile(const std::string& source) {
    auto ctr = GetFileCTR(source);
    auto key = HW::AES::GetNormalKey(HW::AES::SDKey);
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    aes.SetKeyWithIV(key.data(), key.size(), ctr.data());

    FileUtil::IOFile file(root_folder + source, "r");
    auto size = file.GetSize();

    std::vector<u8> encrypted_data(size);
    file.ReadBytes(encrypted_data.data(), size);

    std::vector<u8> data(size);
    aes.ProcessData(data.data(), encrypted_data.data(), encrypted_data.size());
    return data;
}
