#include "hardware_encoder.h"

#include <cstring>

#include "common/log_manager.h"
#include "common/timer_util.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace zenremote {

HardwareEncoder::HardwareEncoder() = default;

HardwareEncoder::~HardwareEncoder() {
  Shutdown();
}

Result<void> HardwareEncoder::Initialize(const EncoderConfig& config) {
  if (initialized_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "Encoder already initialized");
  }

  config_ = config;
  hw_type_ = config.hw_encoder_type;

  // 验证参数
  if (config_.width <= 0 || config_.height <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid video dimensions");
  }

  if (config_.framerate <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter, "Invalid framerate");
  }

  // 自动检测硬件编码器
  if (hw_type_ == HWEncoderType::kNone) {
    hw_type_ = DetectAvailableHWEncoder(config_.codec_id);
    if (hw_type_ == HWEncoderType::kNone) {
      return Result<void>::Err(ErrorCode::kEncoderNotFound,
                               "No hardware encoder available");
    }
  }

  // 获取编码器名称
  const char* enc_name = GetHWEncoderName(hw_type_, config_.codec_id);
  if (!enc_name) {
    return Result<void>::Err(ErrorCode::kEncoderNotFound,
                             "Hardware encoder name not found");
  }

  encoder_name_ = enc_name;

  // 查找编码器
  codec_ = avcodec_find_encoder_by_name(enc_name);
  if (!codec_) {
    return Result<void>::Err(ErrorCode::kEncoderNotFound,
                             fmt::format("Encoder '{}' not found", enc_name));
  }

  ZENREMOTE_INFO("Found hardware encoder: {} ({})", codec_->long_name,
                 HWEncoderTypeToString(hw_type_));

  // 分配编码器上下文
  AVCodecContext* ctx = avcodec_alloc_context3(codec_);
  if (!ctx) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate encoder context");
  }
  codec_ctx_.reset(ctx);

  // 配置编码器
  auto config_result = ConfigureContext();
  if (config_result.IsErr()) {
    return config_result;
  }

  // 设置硬件编码器特定选项
  auto hw_opts_result = SetHWEncoderOptions();
  if (hw_opts_result.IsErr()) {
    return hw_opts_result;
  }

  // 打开编码器
  int ret = avcodec_open2(codec_ctx_.get(), codec_, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(ErrorCode::kEncoderInitFailed,
                             fmt::format("Failed to open encoder: {}", errbuf));
  }

  // 分配输出包
  pkt_.reset(av_packet_alloc());
  if (!pkt_) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate AVPacket");
  }

  initialized_ = true;
  frame_count_ = 0;
  stats_ = EncoderStats{};

  ZENREMOTE_INFO(
      "HardwareEncoder initialized: {} - {}x{} @ {} fps, "
      "bitrate={} kbps",
      encoder_name_, config_.width, config_.height, config_.framerate,
      config_.bitrate / 1000);

  return Result<void>::Ok();
}

Result<void> HardwareEncoder::ConfigureContext() {
  AVCodecContext* ctx = codec_ctx_.get();

  // 基本参数
  ctx->width = config_.width;
  ctx->height = config_.height;
  ctx->time_base = AVRational{1, config_.framerate};
  ctx->framerate = AVRational{config_.framerate, 1};

  // 硬件编码器通常需要 NV12 或特定格式
  // NVENC: NV12, YUV420P, YUV444P
  // QSV: NV12
  // AMF: NV12
  ctx->pix_fmt = config_.input_format;

  // 色彩空间
  ctx->colorspace = config_.color_space;
  ctx->color_primaries = config_.color_primaries;
  ctx->color_trc = config_.color_trc;
  ctx->color_range = config_.color_range;

  // GOP 设置
  ctx->gop_size = config_.gop_size;
  ctx->max_b_frames = config_.max_b_frames;

  // 码率设置
  switch (config_.rate_control) {
    case RateControlMode::kCBR:
      ctx->bit_rate = config_.bitrate;
      ctx->rc_max_rate = config_.bitrate;
      ctx->rc_min_rate = config_.bitrate;
      ctx->rc_buffer_size = config_.bitrate;
      break;

    case RateControlMode::kVBR:
      ctx->bit_rate = config_.bitrate;
      ctx->rc_max_rate = config_.max_bitrate;
      ctx->rc_buffer_size = config_.max_bitrate;
      break;

    case RateControlMode::kCQP:
      // 通过私有选项设置
      break;

    default:
      ctx->bit_rate = config_.bitrate;
      break;
  }

  // 低延迟标志
  ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

  // 禁用 B 帧以降低延迟
  if (config_.max_b_frames == 0) {
    ctx->max_b_frames = 0;
  }

  return Result<void>::Ok();
}

Result<void> HardwareEncoder::SetHWEncoderOptions() {
  switch (hw_type_) {
    case HWEncoderType::kNVENC:
      return SetNVENCOptions();
    case HWEncoderType::kQSV:
      return SetQSVOptions();
    case HWEncoderType::kAMF:
      return SetAMFOptions();
    default:
      return Result<void>::Ok();
  }
}

Result<void> HardwareEncoder::SetNVENCOptions() {
  AVCodecContext* ctx = codec_ctx_.get();

  // NVENC preset
  // p1 = fastest (low quality)
  // p7 = slowest (best quality)
  // For low latency, use p1-p3
  const char* preset = "p1";  // 最快速度
  if (config_.preset == EncoderPreset::kMedium) {
    preset = "p4";
  } else if (config_.preset == EncoderPreset::kSlow) {
    preset = "p6";
  }

  av_opt_set(ctx->priv_data, "preset", preset, 0);

  // 低延迟调优
  av_opt_set(ctx->priv_data, "tune", "ll", 0);  // low latency

  // Profile
  if (config_.profile == "baseline") {
    av_opt_set(ctx->priv_data, "profile", "baseline", 0);
  } else if (config_.profile == "main") {
    av_opt_set(ctx->priv_data, "profile", "main", 0);
  } else {
    av_opt_set(ctx->priv_data, "profile", "high", 0);
  }

  // 码率控制
  if (config_.rate_control == RateControlMode::kCBR) {
    av_opt_set(ctx->priv_data, "rc", "cbr", 0);
  } else if (config_.rate_control == RateControlMode::kVBR) {
    av_opt_set(ctx->priv_data, "rc", "vbr", 0);
  } else if (config_.rate_control == RateControlMode::kCQP) {
    av_opt_set(ctx->priv_data, "rc", "constqp", 0);
    av_opt_set_int(ctx->priv_data, "qp", config_.qp, 0);
  }

  // 禁用 B 帧
  if (config_.max_b_frames == 0) {
    av_opt_set_int(ctx->priv_data, "bf", 0, 0);
  }

  // 零延迟
  if (config_.zero_latency) {
    av_opt_set_int(ctx->priv_data, "zerolatency", 1, 0);
    av_opt_set_int(ctx->priv_data, "delay", 0, 0);
  }

  // GPU 选择（默认使用 0 号 GPU）
  av_opt_set_int(ctx->priv_data, "gpu", 0, 0);

  ZENREMOTE_DEBUG("NVENC options: preset={}, tune=ll, profile={}", preset,
                  config_.profile);

  return Result<void>::Ok();
}

Result<void> HardwareEncoder::SetQSVOptions() {
  AVCodecContext* ctx = codec_ctx_.get();

  // QSV preset
  // veryfast, faster, fast, medium, slow, slower, veryslow
  const char* preset = "veryfast";
  if (config_.preset == EncoderPreset::kMedium) {
    preset = "medium";
  } else if (config_.preset == EncoderPreset::kSlow) {
    preset = "slow";
  }

  av_opt_set(ctx->priv_data, "preset", preset, 0);

  // Profile
  if (config_.profile == "baseline") {
    av_opt_set(ctx->priv_data, "profile", "baseline", 0);
  } else if (config_.profile == "main") {
    av_opt_set(ctx->priv_data, "profile", "main", 0);
  } else {
    av_opt_set(ctx->priv_data, "profile", "high", 0);
  }

  // 低延迟
  av_opt_set_int(ctx->priv_data, "low_delay_brc", 1, 0);

  ZENREMOTE_DEBUG("QSV options: preset={}, profile={}", preset,
                  config_.profile);

  return Result<void>::Ok();
}

Result<void> HardwareEncoder::SetAMFOptions() {
  AVCodecContext* ctx = codec_ctx_.get();

  // AMF usage
  // transcoding, ultralowlatency, lowlatency, webcam
  const char* usage = "ultralowlatency";
  av_opt_set(ctx->priv_data, "usage", usage, 0);

  // Quality preset
  // speed, balanced, quality
  const char* quality = "speed";
  if (config_.preset == EncoderPreset::kMedium) {
    quality = "balanced";
  } else if (config_.preset == EncoderPreset::kSlow) {
    quality = "quality";
  }
  av_opt_set(ctx->priv_data, "quality", quality, 0);

  // Profile
  if (config_.profile == "baseline") {
    av_opt_set(ctx->priv_data, "profile", "baseline", 0);
  } else if (config_.profile == "main") {
    av_opt_set(ctx->priv_data, "profile", "main", 0);
  } else {
    av_opt_set(ctx->priv_data, "profile", "high", 0);
  }

  // 码率控制
  if (config_.rate_control == RateControlMode::kCBR) {
    av_opt_set(ctx->priv_data, "rc", "cbr", 0);
  } else if (config_.rate_control == RateControlMode::kVBR) {
    av_opt_set(ctx->priv_data, "rc", "vbr_peak", 0);
  }

  ZENREMOTE_DEBUG("AMF options: usage={}, quality={}, profile={}", usage,
                  quality, config_.profile);

  return Result<void>::Ok();
}

Result<void> HardwareEncoder::InitHWDeviceContext() {
  // 对于编码，大多数硬件编码器不需要显式创建设备上下文
  // 它们会在 avcodec_open2 时自动创建
  // 仅在需要 GPU 内存帧时才需要设置
  return Result<void>::Ok();
}

void HardwareEncoder::Shutdown() {
  if (!initialized_) {
    return;
  }

  pkt_.reset();
  codec_ctx_.reset();

  if (hw_frames_ctx_) {
    av_buffer_unref(&hw_frames_ctx_);
    hw_frames_ctx_ = nullptr;
  }

  if (hw_device_ctx_) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }

  codec_ = nullptr;
  initialized_ = false;

  ZENREMOTE_INFO("HardwareEncoder ({}) shutdown, encoded {} frames",
                 encoder_name_, stats_.frames_encoded);
}

Result<bool> HardwareEncoder::Encode(AVFrame* frame, EncodedPacket& packet) {
  if (!initialized_) {
    return Result<bool>::Err(ErrorCode::kNotInitialized,
                             "Encoder not initialized");
  }

  TIMER_START(encode);

  // 设置帧时间戳
  if (frame) {
    frame->pts = frame_count_++;

    // 强制关键帧
    if (force_keyframe_) {
      frame->pict_type = AV_PICTURE_TYPE_I;
      force_keyframe_ = false;
    } else {
      frame->pict_type = AV_PICTURE_TYPE_NONE;
    }
  }

  // 发送帧到编码器
  int ret = avcodec_send_frame(codec_ctx_.get(), frame);
  if (ret < 0) {
    if (ret == AVERROR(EAGAIN)) {
      auto output_result = ProcessOutput(packet);
      if (output_result.IsErr()) {
        return Result<bool>::Err(output_result.Code(), output_result.Message());
      }

      ret = avcodec_send_frame(codec_ctx_.get(), frame);
      if (ret < 0 && ret != AVERROR_EOF) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        return Result<bool>::Err(
            ErrorCode::kEncodeFailed,
            fmt::format("avcodec_send_frame failed: {}", errbuf));
      }
    } else if (ret != AVERROR_EOF) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      return Result<bool>::Err(
          ErrorCode::kEncodeFailed,
          fmt::format("avcodec_send_frame failed: {}", errbuf));
    }
  }

  // 接收编码输出
  auto output_result = ProcessOutput(packet);

  double encode_time = TIMER_END_MS(encode);

  if (output_result.IsOk() && output_result.Value()) {
    UpdateStats(pkt_.get(), encode_time);
  }

  return output_result;
}

Result<bool> HardwareEncoder::ProcessOutput(EncodedPacket& packet) {
  av_packet_unref(pkt_.get());

  int ret = avcodec_receive_packet(codec_ctx_.get(), pkt_.get());
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return Result<bool>::Ok(false);
  }

  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<bool>::Err(
        ErrorCode::kEncodeFailed,
        fmt::format("avcodec_receive_packet failed: {}", errbuf));
  }

  // 填充输出包
  packet.data.assign(pkt_->data, pkt_->data + pkt_->size);
  packet.pts = pkt_->pts;
  packet.dts = pkt_->dts;
  packet.duration = pkt_->duration;
  packet.is_keyframe = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;

  return Result<bool>::Ok(true);
}

Result<void> HardwareEncoder::Flush(std::vector<EncodedPacket>& packets) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Encoder not initialized");
  }

  packets.clear();

  int ret = avcodec_send_frame(codec_ctx_.get(), nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kEncodeFailed,
        fmt::format("Failed to flush encoder: {}", errbuf));
  }

  while (true) {
    EncodedPacket packet;
    auto result = ProcessOutput(packet);
    if (result.IsErr()) {
      return Result<void>::Err(result.Code(), result.Message());
    }

    if (!result.Value()) {
      break;
    }

    packets.push_back(std::move(packet));
  }

  ZENREMOTE_DEBUG("HardwareEncoder flushed, {} packets", packets.size());
  return Result<void>::Ok();
}

void HardwareEncoder::ForceKeyFrame() {
  force_keyframe_ = true;
}

Result<void> HardwareEncoder::UpdateBitrate(int bitrate) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Encoder not initialized");
  }

  if (bitrate <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidBitrate, "Invalid bitrate");
  }

  // 硬件编码器的动态码率更新可能需要重新配置
  // 这里只是更新配置，实际效果取决于具体的硬件编码器支持
  codec_ctx_->bit_rate = bitrate;
  codec_ctx_->rc_max_rate = bitrate + bitrate / 4;

  config_.bitrate = bitrate;

  ZENREMOTE_INFO("HardwareEncoder bitrate updated to {} kbps", bitrate / 1000);
  return Result<void>::Ok();
}

void HardwareEncoder::UpdateStats(const AVPacket* pkt, double encode_time_ms) {
  stats_.frames_encoded++;
  if (pkt->flags & AV_PKT_FLAG_KEY) {
    stats_.keyframes_encoded++;
  }

  total_encode_time_ms_ += encode_time_ms;
  stats_.avg_encode_time_ms = total_encode_time_ms_ / stats_.frames_encoded;

  stats_.total_bytes += pkt->size;

  double duration_sec =
      static_cast<double>(stats_.frames_encoded) / config_.framerate;
  if (duration_sec > 0) {
    stats_.avg_bitrate = (stats_.total_bytes * 8.0) / duration_sec;
  }
}

EncoderStats HardwareEncoder::GetStats() const {
  return stats_;
}

bool HardwareEncoder::IsInitialized() const {
  return initialized_;
}

}  // namespace zenremote
