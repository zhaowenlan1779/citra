// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "core/3ds.h"
#include "core/frontend/emu_window.h"
#include "video_core/frame_dumper.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/swrasterizer/swrasterizer.h"
#include "video_core/video_core.h"

#ifdef ENABLE_FRAME_DUMPING
#include "video_core/ffmpeg_frame_dumper.h"
#endif

RendererBase::RendererBase(EmuWindow& window) : render_window{window} {
#ifdef ENABLE_FRAME_DUMPING
    frame_dumpers[0] = std::move(std::make_unique<FrameDumper::FFmpegBackend>());
    frame_dumpers[1] = std::move(std::make_unique<FrameDumper::FFmpegBackend>());
#else
    frame_dumpers[0] = std::move(std::make_unique<FrameDumper::NullBackend>());
    frame_dumpers[1] = std::move(std::make_unique<FrameDumper::NullBackend>());
#endif
}

RendererBase::~RendererBase() = default;
void RendererBase::UpdateCurrentFramebufferLayout() {
    const Layout::FramebufferLayout& layout = render_window.GetFramebufferLayout();
    render_window.UpdateCurrentFramebufferLayout(layout.width, layout.height);
}

void RendererBase::RefreshRasterizerSetting() {
    bool hw_renderer_enabled = VideoCore::g_hw_renderer_enabled;
    if (rasterizer == nullptr || opengl_rasterizer_active != hw_renderer_enabled) {
        opengl_rasterizer_active = hw_renderer_enabled;

        if (hw_renderer_enabled) {
            rasterizer = std::make_unique<OpenGL::RasterizerOpenGL>(render_window);
        } else {
            rasterizer = std::make_unique<VideoCore::SWRasterizer>();
        }
    }
}

bool RendererBase::StartFrameDumping(const std::string& path_top, const std::string& path_bottom,
                                     const std::string& format) {
    if (!frame_dumpers[0]->StartDumping(
            path_top, format, Core::kScreenTopWidth * VideoCore::GetResolutionScaleFactor(),
            Core::kScreenTopHeight * VideoCore::GetResolutionScaleFactor())) {

        return false;
    }
    if (!frame_dumpers[1]->StartDumping(
            path_bottom, format, Core::kScreenBottomWidth * VideoCore::GetResolutionScaleFactor(),
            Core::kScreenBottomHeight * VideoCore::GetResolutionScaleFactor())) {

        return false;
    }
    start_dumping = true;
    return true;
}

void RendererBase::StopFrameDumping() {
    stop_dumping = true;
    frame_dumping_stopped.Wait();
}
