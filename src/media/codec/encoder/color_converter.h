#pragma once

#include <memory>

#include "../../../common/error.h"
#include "../ffmpeg_types.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace zenremote {

/// @brief 色彩空间转换配置
struct ColorConverterConfig {
  // 源格式
  int src_width = 0;
  int src_height = 0;
  AVPixelFormat src_format = AV_PIX_FMT_BGRA;  ///< 屏幕采集通常是 BGRA

  // 目标格式
  int dst_width = 0;                           ///< 0 表示与源相同
  int dst_height = 0;                          ///< 0 表示与源相同
  AVPixelFormat dst_format = AV_PIX_FMT_NV12;  ///< 编码器需要的格式

  // 缩放算法
  int sws_flags = SWS_BILINEAR;  ///< 缩放算法标志
};

/// @brief 色彩空间转换器
///
/// 负责将屏幕采集的 BGRA 格式转换为编码器需要的 NV12/YUV420P 格式。
/// 也可用于分辨率缩放。
class ColorConverter {
 public:
  ColorConverter() = default;
  ~ColorConverter();

  // 禁止拷贝
  ColorConverter(const ColorConverter&) = delete;
  ColorConverter& operator=(const ColorConverter&) = delete;

  // 允许移动
  ColorConverter(ColorConverter&& other) noexcept;
  ColorConverter& operator=(ColorConverter&& other) noexcept;

  /// @brief 初始化转换器
  /// @param config 转换配置
  /// @return 成功返回 Ok，失败返回错误
  Result<void> Initialize(const ColorConverterConfig& config);

  /// @brief 关闭转换器并释放资源
  void Shutdown();

  /// @brief 转换帧
  /// @param src_frame 源帧（BGRA 等）
  /// @param dst_frame 目标帧（NV12 等），必须已分配 buffer
  /// @return 成功返回 Ok
  Result<void> Convert(const AVFrame* src_frame, AVFrame* dst_frame);

  /// @brief 转换帧（自动分配目标帧）
  /// @param src_frame 源帧
  /// @return 转换后的帧或错误
  Result<AVFramePtr> Convert(const AVFrame* src_frame);

  /// @brief 转换原始数据
  /// @param src_data 源数据指针数组
  /// @param src_linesize 源行大小数组
  /// @param dst_frame 目标帧
  /// @return 成功返回 Ok
  Result<void> Convert(const uint8_t* const* src_data,
                       const int* src_linesize,
                       AVFrame* dst_frame);

  /// @brief 检查是否已初始化
  /// @return 如果已初始化返回 true
  bool IsInitialized() const { return sws_ctx_ != nullptr; }

  /// @brief 获取源宽度
  int GetSrcWidth() const { return src_width_; }

  /// @brief 获取源高度
  int GetSrcHeight() const { return src_height_; }

  /// @brief 获取目标宽度
  int GetDstWidth() const { return dst_width_; }

  /// @brief 获取目标高度
  int GetDstHeight() const { return dst_height_; }

  /// @brief 获取源像素格式
  AVPixelFormat GetSrcFormat() const { return src_format_; }

  /// @brief 获取目标像素格式
  AVPixelFormat GetDstFormat() const { return dst_format_; }

  /// @brief 分配目标帧 buffer
  /// @param frame 帧对象
  /// @return 成功返回 Ok
  Result<void> AllocateDstFrame(AVFrame* frame) const;

 private:
  ::SwsContext* sws_ctx_ = nullptr;

  int src_width_ = 0;
  int src_height_ = 0;
  AVPixelFormat src_format_ = AV_PIX_FMT_NONE;

  int dst_width_ = 0;
  int dst_height_ = 0;
  AVPixelFormat dst_format_ = AV_PIX_FMT_NONE;

  int sws_flags_ = SWS_BILINEAR;
};

}  // namespace zenremote
