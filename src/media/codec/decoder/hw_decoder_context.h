#pragma once

#include <memory>

#include "../../../common/error.h"
#include "../ffmpeg_types.h"
#include "hw_decoder_type.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

// 前向声明 D3D11 类型
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace zenremote {

/// @brief 硬件解码器上下文管理
///
/// 负责：
/// 1. 创建和管理 AVHWDeviceContext (D3D11 设备)
/// 2. 在 get_format 回调中动态创建 AVHWFramesContext (帧池)
/// 3. 提供硬件帧到 D3D11 纹理的访问
///
/// 关键设计（参考 zenplay/MPV）：
/// - hw_frames_ctx 在 GetHWFormat 回调中通过 FFmpeg API 动态创建
/// - 使用 avcodec_get_hw_frames_parameters 让 FFmpeg 计算正确的池大小
/// - D3D11 零拷贝需要 SHADER_RESOURCE BindFlag
class HWDecoderContext {
 public:
  HWDecoderContext();
  ~HWDecoderContext();

  // 禁止拷贝
  HWDecoderContext(const HWDecoderContext&) = delete;
  HWDecoderContext& operator=(const HWDecoderContext&) = delete;

  /// @brief 初始化硬件解码上下文
  /// @param type 硬件解码器类型
  /// @param codec_id 编解码器 ID
  /// @param width 视频宽度（用于参考，实际由 FFmpeg 决定）
  /// @param height 视频高度
  /// @return 成功返回 Ok
  Result<void> Initialize(HWDecoderType type,
                          AVCodecID codec_id,
                          int width,
                          int height);

  /// @brief 关闭硬件解码上下文
  void Shutdown();

  /// @brief 配置解码器以使用硬件加速
  /// @param codec_ctx 解码器上下文
  /// @return 成功返回 Ok
  /// @note hw_frames_ctx 将在 get_format 回调中由 FFmpeg 创建
  Result<void> ConfigureDecoder(AVCodecContext* codec_ctx);

  /// @brief 检查是否已初始化
  bool IsInitialized() const { return hw_device_ctx_ != nullptr; }

  /// @brief 获取硬件解码器类型
  HWDecoderType GetType() const { return hw_type_; }

  /// @brief 获取硬件设备上下文
  AVBufferRef* GetDeviceContext() const { return hw_device_ctx_; }

  /// @brief 获取硬件像素格式
  AVPixelFormat GetHWPixelFormat() const { return hw_pix_fmt_; }

  /// @brief 验证硬件帧上下文是否正确配置（用于诊断）
  /// @param codec_ctx 解码器上下文
  /// @return true 如果帧上下文配置正确
  bool ValidateFramesContext(AVCodecContext* codec_ctx) const;

#ifdef _WIN32
  /// @brief 获取 D3D11 设备（仅 Windows）
  ID3D11Device* GetD3D11Device() const { return d3d11_device_; }

  /// @brief 获取 D3D11 设备上下文（仅 Windows）
  ID3D11DeviceContext* GetD3D11DeviceContext() const {
    return d3d11_device_context_;
  }

  /// @brief 从硬件帧获取 D3D11 纹理
  /// @param frame 硬件解码输出的 AVFrame
  /// @return ID3D11Texture2D* (不拥有所有权)，失败返回 nullptr
  ID3D11Texture2D* GetD3D11Texture(AVFrame* frame) const;
#endif

 private:
  /// @brief 获取帧格式协商回调（用于 FFmpeg 的 get_format）
  /// @param ctx 解码器上下文
  /// @param pix_fmts 可用格式列表
  /// @return 选择的像素格式
  static AVPixelFormat GetHWFormat(AVCodecContext* ctx,
                                   const AVPixelFormat* pix_fmts);

  /// @brief 使用 FFmpeg API 初始化硬件加速（MPV 风格）
  /// @param ctx 解码器上下文
  /// @param hw_fmt 目标硬件格式
  /// @return 成功返回 Ok
  Result<void> InitGenericHWAccel(AVCodecContext* ctx, AVPixelFormat hw_fmt);

  /// @brief 创建硬件设备上下文
  Result<void> CreateDeviceContext();

#ifdef _WIN32
  /// @brief 提取 D3D11 设备指针
  void ExtractD3D11Device();

  /// @brief 确保 D3D11 帧上下文有正确的 BindFlags（零拷贝必需）
  /// @param frames_ctx_ref 帧上下文引用
  /// @return 成功返回 true
  bool EnsureD3D11BindFlags(AVBufferRef* frames_ctx_ref);
#endif

  HWDecoderType hw_type_ = HWDecoderType::kNone;
  AVCodecID codec_id_ = AV_CODEC_ID_NONE;
  AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;

  AVBufferRef* hw_device_ctx_ = nullptr;

  // 记录当前生效的 hw_frames_ctx（非拥有）。用于检测 FFmpeg 在运行时替换。
  mutable AVBufferRef* last_hw_frames_ctx_ = nullptr;

  int width_ = 0;
  int height_ = 0;

#ifdef _WIN32
  ID3D11Device* d3d11_device_ = nullptr;
  ID3D11DeviceContext* d3d11_device_context_ = nullptr;
#endif
};

}  // namespace zenremote
