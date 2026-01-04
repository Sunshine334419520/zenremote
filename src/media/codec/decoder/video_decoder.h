#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../../common/error.h"
#include "../ffmpeg_types.h"
#include "hw_decoder_context.h"
#include "hw_decoder_type.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace zenremote {

/// @brief 解码器配置
struct DecoderConfig {
  // 编解码器 ID
  AVCodecID codec_id = AV_CODEC_ID_H264;

  // 是否使用硬件解码
  bool use_hw_decoder = true;

  // 硬件解码器类型（kNone 表示自动检测）
  HWDecoderType hw_decoder_type = HWDecoderType::kNone;

  // 视频尺寸（某些硬件解码器需要）
  int width = 0;
  int height = 0;

  // 线程数（0=自动）
  int thread_count = 0;

  // 额外数据（如 SPS/PPS）
  std::vector<uint8_t> extradata;
};

/// @brief 解码统计信息
struct DecoderStats {
  uint64_t frames_decoded = 0;     ///< 已解码帧数
  uint64_t keyframes_decoded = 0;  ///< 已解码关键帧数
  double avg_decode_time_ms = 0;   ///< 平均解码时间（毫秒）
  uint64_t total_bytes = 0;        ///< 总输入字节数
  bool hw_accel_active = false;    ///< 硬件加速是否激活
};

/// @brief 视频解码器
///
/// 支持软件解码和硬件加速解码（D3D11VA/DXVA2/CUDA等）。
/// 自动检测并使用可用的硬件解码器，失败时回退到软件解码。
class VideoDecoder {
 public:
  VideoDecoder();
  ~VideoDecoder();

  // 禁止拷贝
  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  /// @brief 初始化解码器
  /// @param config 解码器配置
  /// @return 成功返回 Ok
  Result<void> Initialize(const DecoderConfig& config);

  /// @brief 关闭解码器并释放资源
  void Shutdown();

  /// @brief 解码一个数据包
  /// @param data 编码数据
  /// @param size 数据大小
  /// @param pts 显示时间戳
  /// @param dts 解码时间戳
  /// @param[out] frame 输出解码后的帧
  /// @return Ok(true) 有输出帧，Ok(false) 暂无输出，Err 解码错误
  Result<bool> Decode(const uint8_t* data,
                      int size,
                      int64_t pts,
                      int64_t dts,
                      AVFrame* frame);

  /// @brief 解码 AVPacket
  /// @param packet 输入数据包
  /// @param[out] frame 输出帧
  /// @return Ok(true) 有输出帧，Ok(false) 暂无输出
  Result<bool> Decode(const AVPacket* packet, AVFrame* frame);

  /// @brief 刷新解码器，获取所有剩余输出
  /// @param[out] frames 输出帧列表
  /// @return 成功返回 Ok
  Result<void> Flush(std::vector<AVFramePtr>& frames);

  /// @brief 刷新解码器缓冲区（清空但不获取输出）
  void FlushBuffers();

  /// @brief 检查解码器是否已初始化
  bool IsInitialized() const { return initialized_; }

  /// @brief 检查是否使用硬件解码
  bool IsHWAccelerated() const { return hw_context_ != nullptr; }

  /// @brief 获取硬件解码器类型
  HWDecoderType GetHWDecoderType() const;

  /// @brief 获取解码统计信息
  DecoderStats GetStats() const;

  /// @brief 获取视频宽度
  int GetWidth() const;

  /// @brief 获取视频高度
  int GetHeight() const;

  /// @brief 获取像素格式
  AVPixelFormat GetPixelFormat() const;

  /// @brief 获取编解码器名称
  std::string GetCodecName() const;

  /// @brief 获取硬件解码上下文（用于渲染器）
  HWDecoderContext* GetHWContext() const { return hw_context_.get(); }

 private:
  /// @brief 初始化硬件解码
  Result<void> InitHWDecoder(const DecoderConfig& config);

  /// @brief 初始化软件解码
  Result<void> InitSWDecoder(const DecoderConfig& config);

  /// @brief 配置编码器上下文
  Result<void> ConfigureContext(const DecoderConfig& config);

  /// @brief 处理解码输出
  Result<bool> ReceiveFrame(AVFrame* frame);

  /// @brief 更新统计信息
  void UpdateStats(int bytes, double decode_time_ms);

  const AVCodec* codec_ = nullptr;
  AVCodecContextPtr codec_ctx_;
  AVPacketPtr pkt_;

  std::unique_ptr<HWDecoderContext> hw_context_;

  bool initialized_ = false;
  bool hw_init_attempted_ = false;  // 是否已尝试硬件初始化

  DecoderConfig config_;

  // 统计信息
  mutable DecoderStats stats_;
  double total_decode_time_ms_ = 0;
};

}  // namespace zenremote
