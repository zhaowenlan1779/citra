// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include "common/common_types.h"
#include "common/thread.h"
#include "core/core.h"
#include "video_core/frame_dumper.h"
#include "video_core/rasterizer_interface.h"

class EmuWindow;

class RendererBase : NonCopyable {
public:
    /// Used to reference a framebuffer
    enum kFramebuffer { kFramebuffer_VirtualXFB = 0, kFramebuffer_EFB, kFramebuffer_Texture };

    explicit RendererBase(EmuWindow& window);
    virtual ~RendererBase();

    /// Swap buffers (render frame)
    virtual void SwapBuffers() = 0;

    /// Initialize the renderer
    virtual Core::System::ResultStatus Init() = 0;

    /// Shutdown the renderer
    virtual void ShutDown() = 0;

    /// Updates the framebuffer layout of the contained render window handle.
    void UpdateCurrentFramebufferLayout();

    // Getter/setter functions:
    // ------------------------

    f32 GetCurrentFPS() const {
        return m_current_fps;
    }

    int GetCurrentFrame() const {
        return m_current_frame;
    }

    VideoCore::RasterizerInterface* Rasterizer() const {
        return rasterizer.get();
    }

    EmuWindow& GetRenderWindow() const {
        return render_window;
    }

    void RefreshRasterizerSetting();

    bool StartFrameDumping(const std::string& path_top, const std::string& path_bottom,
                           const std::string& format = "");
    void StopFrameDumping();
    bool IsDumpingFrames() const {
        return dump_frames.load(std::memory_order_relaxed);
    }

protected:
    EmuWindow& render_window; ///< Reference to the render window handle.
    std::unique_ptr<VideoCore::RasterizerInterface> rasterizer;
    f32 m_current_fps = 0.0f; ///< Current framerate, should be set by the renderers
    int m_current_frame = 0;  ///< Current frame, should be set by the renderer
    // Frame dumping
    /// Frame dumpers (one for each screen)
    std::array<std::unique_ptr<FrameDumper::Backend>, 2> frame_dumpers;
    std::atomic_bool dump_frames = false;   ///< Whether to dump frames
    std::atomic_bool start_dumping = false; ///< Signal to start dumping frames
    /// Whether to stop dumping. If the renderer receives this singal, it should write an
    /// "end" marker frame to the frame dumping queue
    std::atomic_bool stop_dumping = false;
    /// An event to mark the stopping of frame dumping. This is used to
    /// prevent application being stopped before "end" frame is written
    Common::Event frame_dumping_stopped;

private:
    bool opengl_rasterizer_active = false;
};
