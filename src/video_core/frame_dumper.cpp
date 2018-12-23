// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/frame_dumper.h"

namespace FrameDumper {

FrameData::FrameData(std::size_t width_, std::size_t height_, u8* data_)
    : width(width_), height(height_), data(width * height * 4) {
    // rotate the data by 270 degrees while copying
    stride = width * 4;
    for (std::size_t x = 0; x < height; x++)
        for (std::size_t y = 0; y < width; y++) {
            for (std::size_t k = 0; k < 4; k++) {
                data[(height - 1 - x) * stride + y * 4 + k] = data_[y * height * 4 + x * 4 + k];
            }
        }
}

Backend::~Backend() = default;
NullBackend::~NullBackend() = default;

} // namespace FrameDumper