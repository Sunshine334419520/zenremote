#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

namespace zenremote {

/// @brief 硬件解码器类型枚举
enum class HWDecoderType {
  kNone = 0,      ///< 无硬件加速（软件解码）
  kD3D11VA,       ///< Direct3D 11 Video Acceleration
  kDXVA2,         ///< DirectX Video Acceleration 2.0
  kCUDA,          ///< NVIDIA CUDA
  kVAAPI,         ///< Linux VA-API
  kVDPAU,         ///< Linux VDPAU
  kVideoToolbox,  ///< macOS VideoToolbox
  kQSV,           ///< Intel Quick Sync Video
};

/// @brief 将硬件解码器类型转换为字符串
/// @param type 硬件解码器类型
/// @return 类型的字符串表示
inline const char* HWDecoderTypeToString(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kNone:
      return "None (Software)";
    case HWDecoderType::kD3D11VA:
      return "D3D11VA";
    case HWDecoderType::kDXVA2:
      return "DXVA2";
    case HWDecoderType::kCUDA:
      return "CUDA";
    case HWDecoderType::kVAAPI:
      return "VA-API";
    case HWDecoderType::kVDPAU:
      return "VDPAU";
    case HWDecoderType::kVideoToolbox:
      return "VideoToolbox";
    case HWDecoderType::kQSV:
      return "QSV";
    default:
      return "Unknown";
  }
}

/// @brief 获取硬件解码器对应的 AVHWDeviceType
/// @param type 硬件解码器类型
/// @return AVHWDeviceType，如果不支持返回 AV_HWDEVICE_TYPE_NONE
inline AVHWDeviceType GetAVHWDeviceType(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kD3D11VA:
      return AV_HWDEVICE_TYPE_D3D11VA;
    case HWDecoderType::kDXVA2:
      return AV_HWDEVICE_TYPE_DXVA2;
    case HWDecoderType::kCUDA:
      return AV_HWDEVICE_TYPE_CUDA;
    case HWDecoderType::kVAAPI:
      return AV_HWDEVICE_TYPE_VAAPI;
    case HWDecoderType::kVDPAU:
      return AV_HWDEVICE_TYPE_VDPAU;
    case HWDecoderType::kVideoToolbox:
      return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    case HWDecoderType::kQSV:
      return AV_HWDEVICE_TYPE_QSV;
    default:
      return AV_HWDEVICE_TYPE_NONE;
  }
}

/// @brief 从 AVHWDeviceType 转换为 HWDecoderType
/// @param type AVHWDeviceType
/// @return 对应的 HWDecoderType
inline HWDecoderType GetHWDecoderType(AVHWDeviceType type) {
  switch (type) {
    case AV_HWDEVICE_TYPE_D3D11VA:
      return HWDecoderType::kD3D11VA;
    case AV_HWDEVICE_TYPE_DXVA2:
      return HWDecoderType::kDXVA2;
    case AV_HWDEVICE_TYPE_CUDA:
      return HWDecoderType::kCUDA;
    case AV_HWDEVICE_TYPE_VAAPI:
      return HWDecoderType::kVAAPI;
    case AV_HWDEVICE_TYPE_VDPAU:
      return HWDecoderType::kVDPAU;
    case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
      return HWDecoderType::kVideoToolbox;
    case AV_HWDEVICE_TYPE_QSV:
      return HWDecoderType::kQSV;
    default:
      return HWDecoderType::kNone;
  }
}

/// @brief 获取硬件解码器输出的像素格式
/// @param type 硬件解码器类型
/// @return 硬件帧像素格式
inline AVPixelFormat GetHWPixelFormat(HWDecoderType type) {
  switch (type) {
    case HWDecoderType::kD3D11VA:
      return AV_PIX_FMT_D3D11;
    case HWDecoderType::kDXVA2:
      return AV_PIX_FMT_DXVA2_VLD;
    case HWDecoderType::kCUDA:
      return AV_PIX_FMT_CUDA;
    case HWDecoderType::kVAAPI:
      return AV_PIX_FMT_VAAPI;
    case HWDecoderType::kVDPAU:
      return AV_PIX_FMT_VDPAU;
    case HWDecoderType::kVideoToolbox:
      return AV_PIX_FMT_VIDEOTOOLBOX;
    case HWDecoderType::kQSV:
      return AV_PIX_FMT_QSV;
    default:
      return AV_PIX_FMT_NONE;
  }
}

/// @brief 检测系统上推荐的硬件解码器
/// @return 推荐的硬件解码器类型，如果都不可用则返回 kNone
inline HWDecoderType DetectRecommendedHWDecoder() {
#ifdef _WIN32
  // Windows 优先使用 D3D11VA
  return HWDecoderType::kD3D11VA;
#elif defined(__APPLE__)
  return HWDecoderType::kVideoToolbox;
#elif defined(__linux__)
  return HWDecoderType::kVAAPI;
#else
  return HWDecoderType::kNone;
#endif
}

/// @brief 检查硬件解码器类型是否在系统上可用
/// @param type 要检查的硬件解码器类型
/// @return 如果可用返回 true
inline bool IsHWDecoderAvailable(HWDecoderType type) {
  AVHWDeviceType hw_type = GetAVHWDeviceType(type);
  if (hw_type == AV_HWDEVICE_TYPE_NONE) {
    return false;
  }

  // 尝试创建设备上下文来验证可用性
  AVBufferRef* hw_device_ctx = nullptr;
  int ret =
      av_hwdevice_ctx_create(&hw_device_ctx, hw_type, nullptr, nullptr, 0);

  if (ret >= 0 && hw_device_ctx) {
    av_buffer_unref(&hw_device_ctx);
    return true;
  }

  return false;
}

}  // namespace zenremote
