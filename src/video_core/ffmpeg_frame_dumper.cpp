// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "video_core/ffmpeg_frame_dumper.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace FrameDumper {

/*static*/ bool FFmpegBackend::initialized = false;

FFmpegBackend::FFmpegBackend() {
    if (initialized)
        return;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif
    avformat_network_init();
    initialized = true;
}

FFmpegBackend::~FFmpegBackend() {
    if (frame_processing_thread.joinable())
        frame_processing_thread.join();
}

bool FFmpegBackend::InitializeDumping() {
    if (!FileUtil::CreateFullPath(dump_path)) {
        return false;
    }

    frame_count = 0;

    // Get output format
    // Ensure webm here to avoid patent issues
    ASSERT_MSG(dump_format == "webm", "Only webm is allowed for frame dumping");
    AVOutputFormat* output_format =
        av_guess_format(dump_format.c_str(), dump_path.c_str(), "video/webm");
    if (!output_format) {
        LOG_ERROR(Render, "Could not get format {}", dump_format);
        return false;
    }

    // Initialize format context
    auto* format_context_raw = format_context.get();
    if (avformat_alloc_output_context2(&format_context_raw, output_format, nullptr,
                                       dump_path.c_str()) < 0) {

        LOG_ERROR(Render, "Could not allocate output context");
        return false;
    }
    format_context.reset(format_context_raw);

    // Initialize codec
    // Ensure VP8 codec here, also to avoid patent issues
    constexpr AVCodecID codec_id = AV_CODEC_ID_VP8;
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    codec_context.reset(avcodec_alloc_context3(codec));
    if (!codec || !codec_context) {
        LOG_ERROR(Render, "Could not find encoder or allocate codec context");
        return false;
    }

    // Configure codec context
    codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_context->bit_rate = 2500000;
    codec_context->width = width;
    codec_context->height = height;
    codec_context->time_base.num = 1;
    codec_context->time_base.den = 60;
    codec_context->gop_size = 12;
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    if (output_format->flags & AVFMT_GLOBALHEADER)
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set_int(codec_context.get(), "deadline", 1, 0); // Set to fastest speed

    if (avcodec_open2(codec_context.get(), codec, nullptr) < 0) {
        LOG_ERROR(Render, "Could not open codec");
        return false;
    }

    // Allocate frames
    current_frame.reset(av_frame_alloc());
    scaled_frame.reset(av_frame_alloc());
    scaled_frame->format = codec_context->pix_fmt;
    scaled_frame->width = width;
    scaled_frame->height = height;
    if (av_frame_get_buffer(scaled_frame.get(), 1) < 0) {
        LOG_ERROR(Render, "Could not allocate frame buffer");
        return false;
    }

    // Create stream
    stream.reset(avformat_new_stream(format_context.get(), codec));
    if (!stream || avcodec_parameters_from_context(stream->codecpar, codec_context.get()) < 0) {
        LOG_ERROR(Render, "Could not create stream");
        return false;
    }

    // Open video file
    if (avio_open(&format_context->pb, dump_path.c_str(), AVIO_FLAG_WRITE) < 0 ||
        avformat_write_header(format_context.get(), nullptr)) {

        LOG_ERROR(Render, "Could not open {}", dump_path);
        return false;
    }

    LOG_INFO(Render, "Dumping frames to {} ({}x{})", dump_path, width, height);
    return true;
}

void FFmpegBackend::FreeResources() {
    current_frame.reset();
    scaled_frame.reset();
    codec_context.reset();
    format_context.reset();
    sws_context.reset();
}

void FFmpegBackend::WritePacket(AVPacket& packet) {
    if (packet.pts != static_cast<s64>(AV_NOPTS_VALUE)) {
        packet.pts = av_rescale_q(packet.pts, codec_context->time_base, stream->time_base);
    }
    if (packet.dts != static_cast<s64>(AV_NOPTS_VALUE)) {
        packet.dts = av_rescale_q(packet.dts, codec_context->time_base, stream->time_base);
    }
    packet.stream_index = stream->index;
    av_interleaved_write_frame(format_context.get(), &packet);
}

bool FFmpegBackend::StartDumping(const std::string& path, const std::string& format, int width,
                                 int height) {
    dump_path = path;
    dump_format = format;
    this->width = width;
    this->height = height;
    if (!InitializeDumping()) {
        FreeResources();
        return false;
    }
    if (frame_processing_thread.joinable())
        frame_processing_thread.join();
    frame_processing_thread = std::thread([&] {
        FrameData frame;
        while (true) {
            frame = frame_queue.PopWait();
            if (frame.width == 0 && frame.height == 0) {
                // An empty frame marks the end of frame data
                break;
            }
            ProcessFrame(frame);
        }
        EndDumping();
    });
    return true;
}

void FFmpegBackend::AddFrame(FrameData& frame) {
    frame_queue.Push(std::move(frame));
}

void FFmpegBackend::ProcessFrame(FrameData& frame) {
    if (frame.width != width || frame.height != height) {
        LOG_ERROR(Render, "Frame dropped: resolution does not match");
        return;
    }
    // Prepare frame
    current_frame->data[0] = frame.data.data();
    current_frame->linesize[0] = frame.stride;
    current_frame->format = pixel_format;
    current_frame->width = width;
    current_frame->height = height;

    // Scale the frame
    auto* context =
        sws_getCachedContext(sws_context.get(), width, height, pixel_format, width, height,
                             codec_context->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (context != sws_context.get())
        sws_context.reset(context);

    if (sws_context) {
        sws_scale(sws_context.get(), current_frame->data, current_frame->linesize, 0, height,
                  scaled_frame->data, scaled_frame->linesize);
    }
    scaled_frame->pts = frame_count++;

    // Initialize packet
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    // Encode frame
    if (avcodec_send_frame(codec_context.get(), scaled_frame.get()) < 0) {
        LOG_ERROR(Render, "Frame dropped: could not send frame");
        return;
    }
    int error = avcodec_receive_packet(codec_context.get(), &packet);
    if (error < 0 && error != AVERROR(EAGAIN)) {
        LOG_ERROR(Render, "Frame dropped: could not encode video");
        return;
    }

    // Write frame to video file
    if (error >= 0)
        WritePacket(packet);
}

void FFmpegBackend::EndDumping() {
    LOG_INFO(Render, "Ending frame dumping");
    av_write_trailer(format_context.get());
    FreeResources();
}

} // namespace FrameDumper
