// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "common/color.h"
#include "core/3ds.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/hw/hw.h"
#include "core/hw/lcd.h"
#include "video_core/frame_dumper.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/swrasterizer/swrasterizer.h"
#include "video_core/video_core.h"

RendererBase::RendererBase(EmuWindow& window)
    : render_window{window}, frame_dumpers{std::make_unique<FrameDumper>(),
                                           std::make_unique<FrameDumper>()} {
    FrameDumper::Initialize();
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
            rasterizer = std::make_unique<RasterizerOpenGL>(render_window);
        } else {
            rasterizer = std::make_unique<VideoCore::SWRasterizer>();
        }
    }
}

bool RendererBase::StartFrameDumping(const std::string& path_top, const std::string& path_bottom,
                                     const std::string& format) {
    if (!frame_dumpers[0]->StartDumping(path_top, format, Core::kScreenTopWidth,
                                        Core::kScreenTopHeight)) {
        return false;
    }
    if (!frame_dumpers[1]->StartDumping(path_bottom, format, Core::kScreenBottomWidth,
                                        Core::kScreenBottomHeight)) {
        return false;
    }
    dump_frames = true;
    return true;
}

void RendererBase::StopFrameDumping() {
    if (!dump_frames)
        return;
    dump_frames = false;
    frame_dump_completed.Wait();
    frame_dumpers[0]->StopDumping();
    frame_dumpers[1]->StopDumping();
}

u32 RendererBase::GetColorFillForFramebuffer(int framebuffer_index) {
    // Main LCD (0): 0x1ED02204, Sub LCD (1): 0x1ED02A04
    u32 lcd_color_addr =
        (framebuffer_index == 0) ? LCD_REG_INDEX(color_fill_top) : LCD_REG_INDEX(color_fill_bottom);
    lcd_color_addr = HW::VADDR_LCD + 4 * lcd_color_addr;
    LCD::Regs::ColorFill color_fill = {0};
    LCD::Read(color_fill.raw, lcd_color_addr);
    return color_fill.raw;
}

void RendererBase::DumpFrame() {
    frame_dump_completed.Reset();
    // We swap width and height here as we are going to rotate the image as we copy it
    // 3ds framebuffers are stored rotate 90 degrees
    for (int i : {0, 1}) {
        const auto& framebuffer = GPU::g_regs.framebuffer_config[i];
        FrameDumper::FrameData frame_data{framebuffer.height, framebuffer.width};
        LCD::Regs::ColorFill color_fill{GetColorFillForFramebuffer(i)};
        u8* dest_buffer = frame_data.data.data();
        if (color_fill.is_enabled) {
            std::array<u8, 3> source;
            source[0] = color_fill.color_b;
            source[1] = color_fill.color_g;
            source[2] = color_fill.color_r;
            for (u32 y = 0; y < framebuffer.width; y++) {
                for (u32 x = 0; x < framebuffer.height; x++) {
                    u8* px_dest = dest_buffer + 3 * (x + framebuffer.height * y);
                    std::memcpy(px_dest, source.data(), 3);
                }
            }
        } else {
            const PAddr framebuffer_addr =
                framebuffer.active_fb == 0 ? framebuffer.address_left1 : framebuffer.address_left2;
            Memory::RasterizerFlushRegion(framebuffer_addr,
                                          framebuffer.stride * framebuffer.height);
            const u8* framebuffer_data = Memory::GetPhysicalPointer(framebuffer_addr);
            int bpp = GPU::Regs::BytesPerPixel(framebuffer.color_format);
            // x,y here are in destination pixels
            for (u32 y = 0; y < framebuffer.width; y++) {
                for (u32 x = 0; x < framebuffer.height; x++) {
                    u8* px_dest =
                        dest_buffer + 3 * (x + framebuffer.height * (framebuffer.width - y - 1));
                    const u8* px_source = framebuffer_data + bpp * (y + framebuffer.width * x);
                    Math::Vec4<u8> source;
                    switch (framebuffer.color_format) {
                    case GPU::Regs::PixelFormat::RGB8:
                        source = Color::DecodeRGB8(px_source);
                        break;
                    case GPU::Regs::PixelFormat::RGBA8:
                        source = Color::DecodeRGBA8(px_source);
                        break;
                    case GPU::Regs::PixelFormat::RGBA4:
                        source = Color::DecodeRGBA4(px_source);
                        break;
                    case GPU::Regs::PixelFormat::RGB5A1:
                        source = Color::DecodeRGB5A1(px_source);
                        break;
                    case GPU::Regs::PixelFormat::RGB565:
                        source = Color::DecodeRGB565(px_source);
                        break;
                    }
                    std::memcpy(px_dest, &source, 3);
                }
            }
        }
        frame_dumpers[i]->AddFrame(frame_data);
    }
    frame_dump_completed.Set();
}
