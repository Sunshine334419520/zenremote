#pragma once

#include "../ffmpeg_types.h"
#include "video_encoder.h"

namespace zenremote {

/// @brief libx264 软件视频编码器
///
/// 实现基于 libx264 的 H.264 软件编码。
/// 优势：跨平台、质量稳定、功能全面
/// 劣势：相比硬件编码 CPU 占用高
class SoftwareEncoder : public IVideoEncoder {
 public:
  SoftwareEncoder();
  ~SoftwareEncoder() override;

  // 禁止拷贝
  SoftwareEncoder(const SoftwareEncoder&) = delete;
  SoftwareEncoder& operator=(const SoftwareEncoder&) = delete;

  // IVideoEncoder 接口实现
  Result<void> Initialize(const EncoderConfig& config) override;
  void Shutdown() override;
  Result<bool> Encode(AVFrame* frame, EncodedPacket& packet) override;
  Result<void> Flush(std::vector<EncodedPacket>& packets) override;
  void ForceKeyFrame() override;
  Result<void> UpdateBitrate(int bitrate) override;
  EncoderStats GetStats() const override;
  bool IsInitialized() const override;
  EncoderType GetEncoderType() const override { return EncoderType::kSoftware; }
  std::string GetEncoderName() const override { return encoder_name_; }

 private:
  /// @brief 配置编码器上下文
  Result<void> ConfigureContext();

  /// @brief 设置 libx264 特定选项
  Result<void> SetX264Options();

  /// @brief 处理编码输出
  Result<bool> ProcessOutput(EncodedPacket& packet);

  /// @brief 更新统计信息
  void UpdateStats(const AVPacket* pkt, double encode_time_ms);

  const AVCodec* codec_ = nullptr;
  AVCodecContextPtr codec_ctx_;
  AVPacketPtr pkt_;

  EncoderConfig config_;
  std::string encoder_name_;

  bool initialized_ = false;
  bool force_keyframe_ = false;
  int64_t frame_count_ = 0;

  // 统计信息
  mutable EncoderStats stats_;
  double total_encode_time_ms_ = 0;
};

}  // namespace zenremote
