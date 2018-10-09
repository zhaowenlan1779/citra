// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <memory>
#include <thread>
#include <vector>
#include <boost/optional.hpp>
#include "common/common_types.h"
#include "common/thread.h"
#include "common/threadsafe_queue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

/**
 * Dumps the frames drawn to a video (usually an AVI), at native resolution.
 * Note: each instance of this class only dumps a single screen.
 */
class FrameDumper {
public:
    /**
     * Frame dump data for a single screen
     * data is in RGB888 format, left to right then top to bottom
     */
    class FrameData {
    public:
        size_t width{};
        size_t height{};
        u32 stride{};
        std::vector<u8> data;
        std::unique_ptr<std::thread> frame_copy_thread;
        Common::Signal& complete_signal;
        bool data_ready{};

        FrameData(size_t width_, size_t height_, u8* data_, Common::Signal& complete_signal);
        // FrameData& operator=(FrameData&& another) {
        //     width = another.width;
        //     height = another.height;
        //     stride = another.stride;
        //     data = std::move(another.data);
        //     if (frame_copy_thread && frame_copy_thread->joinable())
        //         frame_copy_thread->join();
        //     frame_copy_thread = std::move(another.frame_copy_thread);
        //     complete_event = another.complete_event;
        //     return *this;
        // }
        ~FrameData();
    };

    /// Initializes ffmpeg libraries. Does nothing if ffmpeg is already initialized.
    static void Initialize();
    void Shutdown();

    bool StartDumping(const std::string& path, const std::string& format, int width, int height);

    void AddFrame(std::unique_ptr<FrameData> frame);

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
    Common::SPSCQueue<std::unique_ptr<FrameData>> frame_queue;

    std::thread frame_processing_thread;

    /// The pixel format the frames are stored in
    static constexpr AVPixelFormat pixel_format = AVPixelFormat::AV_PIX_FMT_BGRA;
};
