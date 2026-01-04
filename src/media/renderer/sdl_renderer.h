#pragma once

#include "common/timer_util.h"
#include "video_renderer.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace zenremote {

/// @brief SDL2 视频渲染器
///
/// 基于 SDL2 的软件/基础硬件加速渲染器。
/// 优势：跨平台、简单易用
/// 劣势：不支持零拷贝，性能一般
class SDLRenderer : public IVideoRenderer {
 public:
  SDLRenderer();
  ~SDLRenderer() override;

  // 禁止拷贝
  SDLRenderer(const SDLRenderer&) = delete;
  SDLRenderer& operator=(const SDLRenderer&) = delete;

  // IVideoRenderer 接口实现
  Result<void> Initialize(const RendererConfig& config) override;
  void Shutdown() override;
  Result<void> Render(const AVFrame* frame) override;
  void Clear() override;
  Result<void> OnResize(int width, int height) override;
  bool IsInitialized() const override { return initialized_; }
  RendererType GetType() const override { return RendererType::kSDL; }
  RenderStats GetStats() const override;
  std::string GetName() const override { return "SDL2 Renderer"; }
  bool SupportsZeroCopy() const override { return false; }

 private:
  /// @brief 创建或重建纹理
  Result<void> CreateTexture(int width, int height, AVPixelFormat format);

  /// @brief 更新统计信息
  void UpdateStats(double render_time_ms);

  /// @brief 将 AVPixelFormat 转换为 SDL 格式
  uint32_t GetSDLPixelFormat(AVPixelFormat format) const;

  SDL_Window* window_ = nullptr;  // 外部窗口，不拥有所有权
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;

  bool initialized_ = false;
  bool owns_window_ = false;

  int video_width_ = 0;
  int video_height_ = 0;
  int window_width_ = 0;
  int window_height_ = 0;
  AVPixelFormat pixel_format_ = AV_PIX_FMT_NONE;

  // 统计信息
  mutable RenderStats stats_;
  double total_render_time_ms_ = 0;
  TimerUtil fps_timer_{};
  uint64_t frames_since_last_update_ = 0;
};

}  // namespace zenremote
