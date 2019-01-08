// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "common/common_types.h"
#include "common/threadsafe_queue.h"
#include "core/dumping/frame_dumper/frame_dumper.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace FrameDumper {

/**
 * Dumps the frames drawn to a video (usually a WebM), at native resolution.
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

    // Deleters
    struct AVFormatContextDeleter {
        void operator()(AVFormatContext* format_context) const {
            avio_closep(&format_context->pb);
            avformat_free_context(format_context);
        }
    };

    struct AVCodecContextDeleter {
        void operator()(AVCodecContext* codec_context) const {
            avcodec_free_context(&codec_context);
        }
    };

    struct AVStreamDeleter {
        void operator()(AVStream* stream) const {
            // Do nothing
        }
    };

    struct AVFrameDeleter {
        void operator()(AVFrame* frame) const {
            av_frame_free(&frame);
        }
    };

    struct SwsContextDeleter {
        void operator()(SwsContext* sws_context) const {
            sws_freeContext(sws_context);
        }
    };

    std::unique_ptr<AVFormatContext, AVFormatContextDeleter> format_context{};
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter> codec_context{};
    std::unique_ptr<AVStream, AVStreamDeleter> stream{};
    std::unique_ptr<AVFrame, AVFrameDeleter> current_frame{};
    std::unique_ptr<AVFrame, AVFrameDeleter> scaled_frame{};
    std::unique_ptr<SwsContext, SwsContextDeleter> sws_context{};

    u64 frame_count{};
    /// Maximum acceptable queue size to avoid eating up memory
    int max_size{};
    Common::SPSCQueue<FrameData> frame_queue;
    std::mutex cv_mutex;
    std::condition_variable cv;

    std::thread frame_processing_thread;

    /// Whether the FFmpeg libraries are already initialized. They can only be initialized
    /// once per application.
    static bool initialized;
    /// The pixel format the frames are stored in
    static constexpr AVPixelFormat pixel_format = AVPixelFormat::AV_PIX_FMT_BGRA;
};

} // namespace FrameDumper
