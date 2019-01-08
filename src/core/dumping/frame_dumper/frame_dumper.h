// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"

namespace FrameDumper {
/**
 * Frame dump data for a single screen
 * data is in RGB888 format, left to right then top to bottom
 */
class FrameData {
public:
    std::size_t width;
    std::size_t height;
    u32 stride;
    std::vector<u8> data;

    FrameData(std::size_t width_ = 0, std::size_t height_ = 0, u8* data_ = nullptr);
};

class Backend {
public:
    virtual ~Backend();
    virtual bool StartDumping(const std::string& path, const std::string& format, int width,
                              int height) = 0;
    virtual void AddFrame(FrameData& frame) = 0;
};

class NullBackend : public Backend {
public:
    ~NullBackend() override;
    bool StartDumping(const std::string& /*path*/, const std::string& /*format*/, int /*width*/,
                      int /*height*/) override {
        return false;
    }
    void AddFrame(FrameData& /*frame*/) {}
};
} // namespace FrameDumper
