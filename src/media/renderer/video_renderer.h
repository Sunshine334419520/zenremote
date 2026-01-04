#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../../common/error.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace zenremote {

// 前向声明
class HWDecoderContext;

/// @brief 渲染器类型
enum class RendererType {
  kSDL,     ///< SDL2 渲染器
  kD3D11,   ///< Direct3D 11 渲染器
  kOpenGL,  ///< OpenGL 渲染器
};

/// @brief 渲染器配置
struct RendererConfig {
  // 窗口句柄（由外部创建）
  void* window_handle = nullptr;

  // 视频尺寸
  int width = 1920;
  int height = 1080;

  // 输入像素格式
  AVPixelFormat input_format = AV_PIX_FMT_NV12;

  // 渲染器类型
  RendererType renderer_type = RendererType::kSDL;

  // 是否启用 VSync
  bool vsync = true;

  // 硬件解码上下文（用于零拷贝渲染）
  HWDecoderContext* hw_context = nullptr;
};

/// @brief 渲染统计信息
struct RenderStats {
  uint64_t frames_rendered = 0;   ///< 已渲染帧数
  uint64_t frames_dropped = 0;    ///< 丢弃帧数
  double avg_render_time_ms = 0;  ///< 平均渲染时间（毫秒）
  double fps = 0;                 ///< 当前帧率
};

/// @brief 视频渲染器接口
///
/// 定义视频渲染的统一接口，支持多种渲染后端。
class IVideoRenderer {
 public:
  virtual ~IVideoRenderer() = default;

  /// @brief 初始化渲染器
  /// @param config 渲染器配置
  /// @return 成功返回 Ok
  virtual Result<void> Initialize(const RendererConfig& config) = 0;

  /// @brief 关闭渲染器
  virtual void Shutdown() = 0;

  /// @brief 渲染一帧
  /// @param frame 要渲染的帧
  /// @return 成功返回 Ok
  virtual Result<void> Render(const AVFrame* frame) = 0;

  /// @brief 清空渲染目标
  virtual void Clear() = 0;

  /// @brief 处理窗口大小改变
  /// @param width 新宽度
  /// @param height 新高度
  /// @return 成功返回 Ok
  virtual Result<void> OnResize(int width, int height) = 0;

  /// @brief 检查是否已初始化
  virtual bool IsInitialized() const = 0;

  /// @brief 获取渲染器类型
  virtual RendererType GetType() const = 0;

  /// @brief 获取渲染统计信息
  virtual RenderStats GetStats() const = 0;

  /// @brief 获取渲染器名称
  virtual std::string GetName() const = 0;

  /// @brief 是否支持零拷贝渲染
  virtual bool SupportsZeroCopy() const = 0;
};

/// @brief 创建视频渲染器
/// @param type 渲染器类型
/// @return 渲染器实例或错误
Result<std::unique_ptr<IVideoRenderer>> CreateVideoRenderer(RendererType type);

/// @brief 渲染器类型转字符串
inline const char* RendererTypeToString(RendererType type) {
  switch (type) {
    case RendererType::kSDL:
      return "SDL";
    case RendererType::kD3D11:
      return "D3D11";
    case RendererType::kOpenGL:
      return "OpenGL";
    default:
      return "Unknown";
  }
}

}  // namespace zenremote
