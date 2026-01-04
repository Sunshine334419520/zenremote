#include "sdl_renderer.h"

#include <SDL2/SDL.h>

#include "common/log_manager.h"
#include "common/timer_util.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

namespace zenremote {

SDLRenderer::SDLRenderer() = default;

SDLRenderer::~SDLRenderer() {
  Shutdown();
}

Result<void> SDLRenderer::Initialize(const RendererConfig& config) {
  if (initialized_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "SDLRenderer already initialized");
  }

  // 初始�?SDL Video 子系�?
  if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
      return Result<void>::Err(
          ErrorCode::kRenderError,
          fmt::format("SDL_InitSubSystem failed: {}", SDL_GetError()));
    }
  }

  video_width_ = config.width;
  video_height_ = config.height;
  pixel_format_ = config.input_format;

  // 使用外部提供的窗口或创建新窗�?
  if (config.window_handle) {
    window_ = SDL_CreateWindowFrom(config.window_handle);
    if (!window_) {
      return Result<void>::Err(
          ErrorCode::kRenderError,
          fmt::format("SDL_CreateWindowFrom failed: {}", SDL_GetError()));
    }
    owns_window_ = false;
  } else {
    window_ = SDL_CreateWindow(
        "ZenRemote Video", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        config.width, config.height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
      return Result<void>::Err(
          ErrorCode::kRenderError,
          fmt::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }
    owns_window_ = true;
  }

  SDL_GetWindowSize(window_, &window_width_, &window_height_);

  // 创建渲染�?
  uint32_t render_flags = SDL_RENDERER_ACCELERATED;
  if (config.vsync) {
    render_flags |= SDL_RENDERER_PRESENTVSYNC;
  }

  renderer_ = SDL_CreateRenderer(window_, -1, render_flags);
  if (!renderer_) {
    ZENREMOTE_WARN("Hardware accelerated renderer failed, trying software");
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  }

  if (!renderer_) {
    if (owns_window_) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("SDL_CreateRenderer failed: {}", SDL_GetError()));
  }

  // 创建纹理
  auto texture_result =
      CreateTexture(video_width_, video_height_, pixel_format_);
  if (texture_result.IsErr()) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (owns_window_) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    return texture_result;
  }

  initialized_ = true;
  stats_ = RenderStats{};
  total_render_time_ms_ = 0;
  frames_since_last_update_ = 0;
  fps_timer_.Reset();

  // 获取渲染器信�?
  SDL_RendererInfo info;
  if (SDL_GetRendererInfo(renderer_, &info) == 0) {
    ZENREMOTE_INFO("SDLRenderer initialized: {} ({}), texture: {}x{}",
                   info.name,
                   (info.flags & SDL_RENDERER_ACCELERATED) ? "HW" : "SW",
                   video_width_, video_height_);
  }

  return Result<void>::Ok();
}

void SDLRenderer::Shutdown() {
  if (!initialized_) {
    return;
  }

  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }

  if (window_ && owns_window_) {
    SDL_DestroyWindow(window_);
  }
  window_ = nullptr;

  initialized_ = false;
  ZENREMOTE_INFO("SDLRenderer shutdown, rendered {} frames",
                 stats_.frames_rendered);
}

Result<void> SDLRenderer::CreateTexture(int width,
                                        int height,
                                        AVPixelFormat format) {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  uint32_t sdl_format = GetSDLPixelFormat(format);
  if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
    return Result<void>::Err(ErrorCode::kUnsupportedPixelFormat,
                             "Unsupported pixel format for SDL");
  }

  texture_ = SDL_CreateTexture(renderer_, sdl_format,
                               SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture_) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("SDL_CreateTexture failed: {}", SDL_GetError()));
  }

  video_width_ = width;
  video_height_ = height;
  pixel_format_ = format;

  return Result<void>::Ok();
}

uint32_t SDLRenderer::GetSDLPixelFormat(AVPixelFormat format) const {
  switch (format) {
    case AV_PIX_FMT_NV12:
      return SDL_PIXELFORMAT_NV12;
    case AV_PIX_FMT_NV21:
      return SDL_PIXELFORMAT_NV21;
    case AV_PIX_FMT_YUV420P:
      return SDL_PIXELFORMAT_IYUV;
    case AV_PIX_FMT_YUYV422:
      return SDL_PIXELFORMAT_YUY2;
    case AV_PIX_FMT_UYVY422:
      return SDL_PIXELFORMAT_UYVY;
    case AV_PIX_FMT_RGB24:
      return SDL_PIXELFORMAT_RGB24;
    case AV_PIX_FMT_BGR24:
      return SDL_PIXELFORMAT_BGR24;
    case AV_PIX_FMT_RGBA:
      return SDL_PIXELFORMAT_RGBA32;
    case AV_PIX_FMT_BGRA:
      return SDL_PIXELFORMAT_BGRA32;
    case AV_PIX_FMT_ARGB:
      return SDL_PIXELFORMAT_ARGB32;
    case AV_PIX_FMT_ABGR:
      return SDL_PIXELFORMAT_ABGR32;
    default:
      return SDL_PIXELFORMAT_UNKNOWN;
  }
}

Result<void> SDLRenderer::Render(const AVFrame* frame) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "SDLRenderer not initialized");
  }

  if (!frame) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Null frame pointer");
  }

  TIMER_START(render);

  // 检查尺寸是否改�?
  if (frame->width != video_width_ || frame->height != video_height_) {
    auto texture_result = CreateTexture(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format));
    if (texture_result.IsErr()) {
      return texture_result;
    }
  }

  // 更新纹理
  int ret = 0;
  AVPixelFormat pix_fmt = static_cast<AVPixelFormat>(frame->format);

  if (pix_fmt == AV_PIX_FMT_NV12 || pix_fmt == AV_PIX_FMT_NV21) {
    // NV12/NV21: Y 平面 + UV 交错平面
    ret = SDL_UpdateNVTexture(texture_, nullptr, frame->data[0],
                              frame->linesize[0], frame->data[1],
                              frame->linesize[1]);
  } else if (pix_fmt == AV_PIX_FMT_YUV420P) {
    // YUV420P: 三个平面
    ret = SDL_UpdateYUVTexture(
        texture_, nullptr, frame->data[0], frame->linesize[0], frame->data[1],
        frame->linesize[1], frame->data[2], frame->linesize[2]);
  } else {
    // 其他格式：单平面
    ret = SDL_UpdateTexture(texture_, nullptr, frame->data[0],
                            frame->linesize[0]);
  }

  if (ret < 0) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("SDL texture update failed: {}", SDL_GetError()));
  }

  // 清除并渲�?
  SDL_RenderClear(renderer_);

  // 计算保持宽高比的目标矩形
  SDL_Rect dst_rect;
  float video_aspect = static_cast<float>(video_width_) / video_height_;
  float window_aspect = static_cast<float>(window_width_) / window_height_;

  if (video_aspect > window_aspect) {
    // 视频更宽，以宽度为准
    dst_rect.w = window_width_;
    dst_rect.h = static_cast<int>(window_width_ / video_aspect);
    dst_rect.x = 0;
    dst_rect.y = (window_height_ - dst_rect.h) / 2;
  } else {
    // 视频更高，以高度为准
    dst_rect.h = window_height_;
    dst_rect.w = static_cast<int>(window_height_ * video_aspect);
    dst_rect.x = (window_width_ - dst_rect.w) / 2;
    dst_rect.y = 0;
  }

  SDL_RenderCopy(renderer_, texture_, nullptr, &dst_rect);
  SDL_RenderPresent(renderer_);

  double render_time = TIMER_END_MS(render);

  UpdateStats(render_time);

  return Result<void>::Ok();
}

void SDLRenderer::Clear() {
  if (renderer_) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
  }
}

Result<void> SDLRenderer::OnResize(int width, int height) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "SDLRenderer not initialized");
  }

  window_width_ = width;
  window_height_ = height;

  ZENREMOTE_DEBUG("SDLRenderer resized to {}x{}", width, height);
  return Result<void>::Ok();
}

void SDLRenderer::UpdateStats(double render_time_ms) {
  stats_.frames_rendered++;
  frames_since_last_update_++;

  total_render_time_ms_ += render_time_ms;
  stats_.avg_render_time_ms = total_render_time_ms_ / stats_.frames_rendered;

  // 每秒更新 FPS
  const int64_t elapsed_ms = fps_timer_.ElapsedMsInt();
  if (elapsed_ms >= 1000) {
    stats_.fps = frames_since_last_update_ * 1000.0 / elapsed_ms;
    fps_timer_.Reset();
    frames_since_last_update_ = 0;
  }
}

RenderStats SDLRenderer::GetStats() const {
  return stats_;
}

}  // namespace zenremote
