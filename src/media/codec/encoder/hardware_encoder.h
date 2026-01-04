#pragma once

#include "../ffmpeg_types.h"
#include "hw_encoder_type.h"
#include "video_encoder.h"

namespace zenremote {

/// @brief 硬件加速视频编码器
///
/// 支持 NVIDIA NVENC, Intel QSV, AMD AMF 等硬件编码器。
/// 优势：低 CPU 占用，高性能
/// 劣势：依赖硬件支持
class HardwareEncoder : public IVideoEncoder {
 public:
  HardwareEncoder();
  ~HardwareEncoder() override;

  // 禁止拷贝
  HardwareEncoder(const HardwareEncoder&) = delete;
  HardwareEncoder& operator=(const HardwareEncoder&) = delete;

  // IVideoEncoder 接口实现
  Result<void> Initialize(const EncoderConfig& config) override;
  void Shutdown() override;
  Result<bool> Encode(AVFrame* frame, EncodedPacket& packet) override;
  Result<void> Flush(std::vector<EncodedPacket>& packets) override;
  void ForceKeyFrame() override;
  Result<void> UpdateBitrate(int bitrate) override;
  EncoderStats GetStats() const override;
  bool IsInitialized() const override;
  EncoderType GetEncoderType() const override { return EncoderType::kHardware; }
  std::string GetEncoderName() const override { return encoder_name_; }

  /// @brief 获取硬件编码器类型
  HWEncoderType GetHWEncoderType() const { return hw_type_; }

 private:
  /// @brief 配置编码器上下文
  Result<void> ConfigureContext();

  /// @brief 设置硬件编码器特定选项
  Result<void> SetHWEncoderOptions();

  /// @brief 设置 NVENC 特定选项
  Result<void> SetNVENCOptions();

  /// @brief 设置 QSV 特定选项
  Result<void> SetQSVOptions();

  /// @brief 设置 AMF 特定选项
  Result<void> SetAMFOptions();

  /// @brief 初始化硬件设备上下文
  Result<void> InitHWDeviceContext();

  /// @brief 处理编码输出
  Result<bool> ProcessOutput(EncodedPacket& packet);

  /// @brief 更新统计信息
  void UpdateStats(const AVPacket* pkt, double encode_time_ms);

  const AVCodec* codec_ = nullptr;
  AVCodecContextPtr codec_ctx_;
  AVPacketPtr pkt_;

  // 硬件设备上下文
  AVBufferRef* hw_device_ctx_ = nullptr;
  AVBufferRef* hw_frames_ctx_ = nullptr;

  EncoderConfig config_;
  HWEncoderType hw_type_ = HWEncoderType::kNone;
  std::string encoder_name_;

  bool initialized_ = false;
  bool force_keyframe_ = false;
  int64_t frame_count_ = 0;

  // 统计信息
  mutable EncoderStats stats_;
  double total_encode_time_ms_ = 0;
};

}  // namespace zenremote
