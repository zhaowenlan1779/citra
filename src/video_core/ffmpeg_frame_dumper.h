// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <thread>
#include <vector>
#include "common/common_types.h"
#include "common/threadsafe_queue.h"
#include "video_core/frame_dumper.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace FrameDumper {

/**
 * Dumps the frames drawn to a video (usually an AVI), at native resolution.
 * Note: each instance of this class only dumps a single screen.
 */
class FFmpegBackend : public Backend {
public:
    FFmpegBackend();
    ~FFmpegBackend() override;
    bool StartDumping(const std::string& path, const std::string& format, int width,
                      int height) override;
    void AddFrame(FrameData& frame) override;

private:
    bool InitializeDumping();
    void FreeResources();
    /// Writes the encoded frame to the video file
    void WritePacket(AVPacket& packet);
    void ProcessFrame(FrameData& frame);
    void EndDumping();

    int width, height;
    std::string dump_path;
    std::string dump_format;

    AVFormatContext* format_context{};
    AVCodecContext* codec_context{};
    AVStream* stream{};
    AVFrame* current_frame{};
    AVFrame* scaled_frame{};
    SwsContext* sws_context{};

    u64 frame_count{};
    Common::SPSCQueue<FrameData> frame_queue;

    std::thread frame_processing_thread;

    /// Whether the FFmpeg libraries are already initialized. They can only be initialized
    /// once per application.
    static bool initialized;
    /// The pixel format the frames are stored in
    static constexpr AVPixelFormat pixel_format = AVPixelFormat::AV_PIX_FMT_BGRA;
};

} // namespace FrameDumper
