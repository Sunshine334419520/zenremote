#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../../common/error.h"
#include "../ffmpeg_types.h"
#include "hw_encoder_type.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace zenremote {

/// @brief 编码器类型枚举
enum class EncoderType {
  kSoftware,  ///< 软件编码 (libx264)
  kHardware,  ///< 硬件编码 (NVENC/QSV/AMF)
};

/// @brief 码率控制模式
enum class RateControlMode {
  kCBR,  ///< 恒定码率
  kVBR,  ///< 可变码率
  kCRF,  ///< 恒定质量因子（仅软件编码）
  kCQP,  ///< 恒定量化参数
};

/// @brief 编码预设（速度与质量权衡）
enum class EncoderPreset {
  kUltrafast,
  kSuperfast,
  kVeryfast,
  kFaster,
  kFast,
  kMedium,
  kSlow,
  kSlower,
  kVeryslow,
  kLowLatency,  ///< 低延迟专用预设
};

/// @brief 编码器配置
struct EncoderConfig {
  // 视频尺寸
  int width = 1920;
  int height = 1080;

  // 帧率
  int framerate = 60;

  // 输入像素格式
  AVPixelFormat input_format = AV_PIX_FMT_NV12;

  // 编码器类型
  EncoderType encoder_type = EncoderType::kSoftware;

  // 硬件编码器类型（仅当 encoder_type == kHardware 时使用）
  HWEncoderType hw_encoder_type = HWEncoderType::kNone;

  // 编解码器 ID
  AVCodecID codec_id = AV_CODEC_ID_H264;

  // 码率控制
  RateControlMode rate_control = RateControlMode::kVBR;
  int bitrate = 8000000;       ///< 目标码率（bps）
  int max_bitrate = 12000000;  ///< 最大码率（bps）
  int crf = 23;                ///< CRF 值（0-51，越低质量越好）
  int qp = 23;                 ///< QP 值

  // 编码预设
  EncoderPreset preset = EncoderPreset::kLowLatency;

  // H.264 Profile
  std::string profile = "high";  ///< "baseline", "main", "high"

  // GOP 设置
  int gop_size = 120;    ///< 关键帧间隔
  int max_b_frames = 0;  ///< B帧数量（远程桌面通常为0）

  // 低延迟设置
  bool zero_latency = true;  ///< 零延迟模式（禁用 lookahead）
  int thread_count = 0;      ///< 线程数（0=自动）

  // 色彩空间
  AVColorSpace color_space = AVCOL_SPC_BT709;
  AVColorPrimaries color_primaries = AVCOL_PRI_BT709;
  AVColorTransferCharacteristic color_trc = AVCOL_TRC_BT709;
  AVColorRange color_range =
      AVCOL_RANGE_JPEG;  ///< Full range for screen content
};

/// @brief 编码输出数据包
struct EncodedPacket {
  std::vector<uint8_t> data;  ///< 编码后的数据
  int64_t pts = 0;            ///< 显示时间戳
  int64_t dts = 0;            ///< 解码时间戳
  bool is_keyframe = false;   ///< 是否为关键帧
  int64_t duration = 0;       ///< 帧时长
};

/// @brief 编码统计信息
struct EncoderStats {
  uint64_t frames_encoded = 0;     ///< 已编码帧数
  uint64_t keyframes_encoded = 0;  ///< 已编码关键帧数
  double avg_encode_time_ms = 0;   ///< 平均编码时间（毫秒）
  double avg_bitrate = 0;          ///< 实际平均码率
  uint64_t total_bytes = 0;        ///< 总输出字节数
};

/// @brief 视频编码器接口
class IVideoEncoder {
 public:
  virtual ~IVideoEncoder() = default;

  /// @brief 初始化编码器
  /// @param config 编码器配置
  /// @return 成功返回 true
  virtual Result<void> Initialize(const EncoderConfig& config) = 0;

  /// @brief 关闭编码器并释放资源
  virtual void Shutdown() = 0;

  /// @brief 编码一帧视频
  /// @param frame 输入视频帧（必须是配置中指定的像素格式）
  /// @param[out] packet 输出编码后的数据包
  /// @return 成功返回 true，如果没有输出返回 false（不是错误）
  virtual Result<bool> Encode(AVFrame* frame, EncodedPacket& packet) = 0;

  /// @brief 刷新编码器，获取所有剩余的输出
  /// @param packets 输出编码后的数据包列表
  /// @return 成功返回 true
  virtual Result<void> Flush(std::vector<EncodedPacket>& packets) = 0;

  /// @brief 强制生成关键帧
  virtual void ForceKeyFrame() = 0;

  /// @brief 动态更新码率
  /// @param bitrate 新的目标码率（bps）
  /// @return 成功返回 true
  virtual Result<void> UpdateBitrate(int bitrate) = 0;

  /// @brief 获取编码统计信息
  /// @return 编码器统计数据
  virtual EncoderStats GetStats() const = 0;

  /// @brief 检查编码器是否已初始化
  /// @return 如果已初始化返回 true
  virtual bool IsInitialized() const = 0;

  /// @brief 获取编码器类型
  /// @return 编码器类型
  virtual EncoderType GetEncoderType() const = 0;

  /// @brief 获取编码器名称
  /// @return 编码器名称字符串
  virtual std::string GetEncoderName() const = 0;
};

/// @brief 创建视频编码器
/// @param config 编码器配置
/// @return 编码器实例或错误
Result<std::unique_ptr<IVideoEncoder>> CreateVideoEncoder(
    const EncoderConfig& config);

/// @brief 编码预设转换为 FFmpeg preset 字符串
inline const char* EncoderPresetToString(EncoderPreset preset) {
  switch (preset) {
    case EncoderPreset::kUltrafast:
      return "ultrafast";
    case EncoderPreset::kSuperfast:
      return "superfast";
    case EncoderPreset::kVeryfast:
      return "veryfast";
    case EncoderPreset::kFaster:
      return "faster";
    case EncoderPreset::kFast:
      return "fast";
    case EncoderPreset::kMedium:
      return "medium";
    case EncoderPreset::kSlow:
      return "slow";
    case EncoderPreset::kSlower:
      return "slower";
    case EncoderPreset::kVeryslow:
      return "veryslow";
    case EncoderPreset::kLowLatency:
      return "ultrafast";  // 低延迟使用 ultrafast
    default:
      return "medium";
  }
}

}  // namespace zenremote
