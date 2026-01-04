#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace zenremote {

/// @brief 硬件编码器类型枚举
enum class HWEncoderType {
  kNone = 0,      ///< 无硬件加速（软件编码）
  kNVENC,         ///< NVIDIA NVENC
  kQSV,           ///< Intel Quick Sync Video
  kAMF,           ///< AMD Advanced Media Framework
  kVideoToolbox,  ///< macOS VideoToolbox
  kVAAPI,         ///< Linux VA-API
};

/// @brief 将硬件编码器类型转换为字符串
/// @param type 硬件编码器类型
/// @return 类型的字符串表示
inline const char* HWEncoderTypeToString(HWEncoderType type) {
  switch (type) {
    case HWEncoderType::kNone:
      return "None (Software)";
    case HWEncoderType::kNVENC:
      return "NVENC";
    case HWEncoderType::kQSV:
      return "QSV";
    case HWEncoderType::kAMF:
      return "AMF";
    case HWEncoderType::kVideoToolbox:
      return "VideoToolbox";
    case HWEncoderType::kVAAPI:
      return "VA-API";
    default:
      return "Unknown";
  }
}

/// @brief 获取硬件编码器对应的 FFmpeg 编码器名称
/// @param type 硬件编码器类型
/// @param codec_id 视频编解码器 ID（如 AV_CODEC_ID_H264）
/// @return FFmpeg 编码器名称，如果不支持则返回 nullptr
inline const char* GetHWEncoderName(HWEncoderType type, AVCodecID codec_id) {
  if (codec_id == AV_CODEC_ID_H264) {
    switch (type) {
      case HWEncoderType::kNVENC:
        return "h264_nvenc";
      case HWEncoderType::kQSV:
        return "h264_qsv";
      case HWEncoderType::kAMF:
        return "h264_amf";
      case HWEncoderType::kVideoToolbox:
        return "h264_videotoolbox";
      case HWEncoderType::kVAAPI:
        return "h264_vaapi";
      default:
        return nullptr;
    }
  } else if (codec_id == AV_CODEC_ID_HEVC) {
    switch (type) {
      case HWEncoderType::kNVENC:
        return "hevc_nvenc";
      case HWEncoderType::kQSV:
        return "hevc_qsv";
      case HWEncoderType::kAMF:
        return "hevc_amf";
      case HWEncoderType::kVideoToolbox:
        return "hevc_videotoolbox";
      case HWEncoderType::kVAAPI:
        return "hevc_vaapi";
      default:
        return nullptr;
    }
  }
  return nullptr;
}

/// @brief 检查指定的硬件编码器是否可用
/// @param type 硬件编码器类型
/// @param codec_id 视频编解码器 ID
/// @return 如果编码器可用返回 true
inline bool IsHWEncoderAvailable(HWEncoderType type,
                                 AVCodecID codec_id = AV_CODEC_ID_H264) {
  const char* encoder_name = GetHWEncoderName(type, codec_id);
  if (!encoder_name) {
    return false;
  }

  const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name);
  return codec != nullptr;
}

/// @brief 检测系统上可用的硬件编码器
/// @param codec_id 要检查的编解码器类型
/// @return 第一个可用的硬件编码器类型，如果都不可用则返回 kNone
inline HWEncoderType DetectAvailableHWEncoder(
    AVCodecID codec_id = AV_CODEC_ID_H264) {
  // 按优先级顺序检查
  static const HWEncoderType types[] = {
      HWEncoderType::kNVENC,        HWEncoderType::kQSV,   HWEncoderType::kAMF,
      HWEncoderType::kVideoToolbox, HWEncoderType::kVAAPI,
  };

  for (auto type : types) {
    if (IsHWEncoderAvailable(type, codec_id)) {
      return type;
    }
  }

  return HWEncoderType::kNone;
}

}  // namespace zenremote
