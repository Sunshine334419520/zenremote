#include "software_encoder.h"

#include <cstring>

#include "common/log_manager.h"
#include "common/timer_util.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace zenremote {

SoftwareEncoder::SoftwareEncoder() = default;

SoftwareEncoder::~SoftwareEncoder() {
  Shutdown();
}

Result<void> SoftwareEncoder::Initialize(const EncoderConfig& config) {
  if (initialized_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "Encoder already initialized");
  }

  config_ = config;

  // 验证参数
  if (config_.width <= 0 || config_.height <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid video dimensions");
  }

  if (config_.framerate <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter, "Invalid framerate");
  }

  // 查找编码器
  const char* encoder_name = "libx264";
  if (config_.codec_id == AV_CODEC_ID_HEVC) {
    encoder_name = "libx265";
  }

  codec_ = avcodec_find_encoder_by_name(encoder_name);
  if (!codec_) {
    return Result<void>::Err(
        ErrorCode::kEncoderNotFound,
        fmt::format("Encoder '{}' not found", encoder_name));
  }

  encoder_name_ = encoder_name;
  ZENREMOTE_INFO("Found encoder: {}", codec_->long_name);

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

  // 设置 x264 特定选项
  auto x264_result = SetX264Options();
  if (x264_result.IsErr()) {
    return x264_result;
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
      "SoftwareEncoder initialized: {}x{} @ {} fps, profile={}, "
      "bitrate={} kbps",
      config_.width, config_.height, config_.framerate, config_.profile,
      config_.bitrate / 1000);

  return Result<void>::Ok();
}

Result<void> SoftwareEncoder::ConfigureContext() {
  AVCodecContext* ctx = codec_ctx_.get();

  // 基本参数
  ctx->width = config_.width;
  ctx->height = config_.height;
  ctx->time_base = AVRational{1, config_.framerate};
  ctx->framerate = AVRational{config_.framerate, 1};
  ctx->pix_fmt = config_.input_format;

  // 色彩空间
  ctx->colorspace = config_.color_space;
  ctx->color_primaries = config_.color_primaries;
  ctx->color_trc = config_.color_trc;
  ctx->color_range = config_.color_range;

  // GOP 设置
  ctx->gop_size = config_.gop_size;
  ctx->max_b_frames = config_.max_b_frames;

  // 线程设置
  if (config_.thread_count > 0) {
    ctx->thread_count = config_.thread_count;
  } else {
    ctx->thread_count = 0;  // 自动检测
  }
  ctx->thread_type = FF_THREAD_SLICE;  // 切片级并行

  // 码率控制
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

    case RateControlMode::kCRF:
      // CRF 通过私有选项设置
      break;

    case RateControlMode::kCQP:
      ctx->qmin = config_.qp;
      ctx->qmax = config_.qp;
      break;
  }

  // Profile
  if (config_.profile == "baseline") {
    ctx->profile = FF_PROFILE_H264_BASELINE;
  } else if (config_.profile == "main") {
    ctx->profile = FF_PROFILE_H264_MAIN;
  } else if (config_.profile == "high") {
    ctx->profile = FF_PROFILE_H264_HIGH;
  }

  // 额外标志
  ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;  // 低延迟

  return Result<void>::Ok();
}

Result<void> SoftwareEncoder::SetX264Options() {
  AVCodecContext* ctx = codec_ctx_.get();

  // preset
  const char* preset = EncoderPresetToString(config_.preset);
  int ret = av_opt_set(ctx->priv_data, "preset", preset, 0);
  if (ret < 0) {
    ZENREMOTE_WARN("Failed to set x264 preset: {}", preset);
  }

  // tune for low latency
  if (config_.zero_latency) {
    ret = av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
    if (ret < 0) {
      ZENREMOTE_WARN("Failed to set x264 tune: zerolatency");
    }
  }

  // CRF mode
  if (config_.rate_control == RateControlMode::kCRF) {
    ret = av_opt_set_int(ctx->priv_data, "crf", config_.crf, 0);
    if (ret < 0) {
      ZENREMOTE_WARN("Failed to set x264 crf: {}", config_.crf);
    }
  }

  // Profile 字符串
  ret = av_opt_set(ctx->priv_data, "profile", config_.profile.c_str(), 0);
  if (ret < 0) {
    ZENREMOTE_WARN("Failed to set x264 profile: {}", config_.profile);
  }

  // 禁用 B-frames 以降低延迟
  if (config_.max_b_frames == 0) {
    av_opt_set_int(ctx->priv_data, "b-frames", 0, 0);
    av_opt_set_int(ctx->priv_data, "b-adapt", 0, 0);
  }

  // 禁用前向预测以降低延迟
  if (config_.zero_latency) {
    av_opt_set_int(ctx->priv_data, "lookahead", 0, 0);
    av_opt_set_int(ctx->priv_data, "rc-lookahead", 0, 0);
  }

  // 快速首帧
  av_opt_set_int(ctx->priv_data, "intra-refresh", 1, 0);

  return Result<void>::Ok();
}

void SoftwareEncoder::Shutdown() {
  if (!initialized_) {
    return;
  }

  // 编码器会在 avcodec_free_context 时自动关闭
  pkt_.reset();
  codec_ctx_.reset();
  codec_ = nullptr;

  initialized_ = false;
  ZENREMOTE_INFO("SoftwareEncoder shutdown, encoded {} frames",
                 stats_.frames_encoded);
}

Result<bool> SoftwareEncoder::Encode(AVFrame* frame, EncodedPacket& packet) {
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
      // 需要先接收输出
      auto output_result = ProcessOutput(packet);
      if (output_result.IsErr()) {
        return Result<bool>::Err(output_result.Code(), output_result.Message());
      }

      // 重试发送
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

Result<bool> SoftwareEncoder::ProcessOutput(EncodedPacket& packet) {
  av_packet_unref(pkt_.get());

  int ret = avcodec_receive_packet(codec_ctx_.get(), pkt_.get());
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return Result<bool>::Ok(false);  // 暂无输出
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

Result<void> SoftwareEncoder::Flush(std::vector<EncodedPacket>& packets) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Encoder not initialized");
  }

  packets.clear();

  // 发送空帧以刷新编码器
  int ret = avcodec_send_frame(codec_ctx_.get(), nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kEncodeFailed,
        fmt::format("Failed to flush encoder: {}", errbuf));
  }

  // 接收所有剩余输出
  while (true) {
    EncodedPacket packet;
    auto result = ProcessOutput(packet);
    if (result.IsErr()) {
      return Result<void>::Err(result.Code(), result.Message());
    }

    if (!result.Value()) {
      break;  // 没有更多输出
    }

    packets.push_back(std::move(packet));
  }

  ZENREMOTE_DEBUG("Encoder flushed, {} packets", packets.size());
  return Result<void>::Ok();
}

void SoftwareEncoder::ForceKeyFrame() {
  force_keyframe_ = true;
}

Result<void> SoftwareEncoder::UpdateBitrate(int bitrate) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Encoder not initialized");
  }

  if (bitrate <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidBitrate, "Invalid bitrate");
  }

  // 动态更新码率（libx264 支持）
  codec_ctx_->bit_rate = bitrate;
  codec_ctx_->rc_max_rate = bitrate + bitrate / 4;  // 25% 余量

  config_.bitrate = bitrate;

  ZENREMOTE_INFO("Encoder bitrate updated to {} kbps", bitrate / 1000);
  return Result<void>::Ok();
}

void SoftwareEncoder::UpdateStats(const AVPacket* pkt, double encode_time_ms) {
  stats_.frames_encoded++;
  if (pkt->flags & AV_PKT_FLAG_KEY) {
    stats_.keyframes_encoded++;
  }

  total_encode_time_ms_ += encode_time_ms;
  stats_.avg_encode_time_ms = total_encode_time_ms_ / stats_.frames_encoded;

  stats_.total_bytes += pkt->size;

  // 计算平均码率（假设固定帧率）
  double duration_sec =
      static_cast<double>(stats_.frames_encoded) / config_.framerate;
  if (duration_sec > 0) {
    stats_.avg_bitrate = (stats_.total_bytes * 8.0) / duration_sec;
  }
}

EncoderStats SoftwareEncoder::GetStats() const {
  return stats_;
}

bool SoftwareEncoder::IsInitialized() const {
  return initialized_;
}

}  // namespace zenremote
