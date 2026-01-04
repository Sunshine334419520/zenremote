#include "video_decoder.h"

#include "common/log_manager.h"
#include "common/timer_util.h"

namespace zenremote {

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() {
  Shutdown();
}

Result<void> VideoDecoder::Initialize(const DecoderConfig& config) {
  if (initialized_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "Decoder already initialized");
  }

  config_ = config;

  // 首先尝试硬件解码（如果启用）
  if (config.use_hw_decoder && !hw_init_attempted_) {
    auto hw_result = InitHWDecoder(config);
    hw_init_attempted_ = true;

    if (hw_result.IsOk()) {
      initialized_ = true;
      stats_.hw_accel_active = true;
      ZENREMOTE_INFO("VideoDecoder initialized with hardware acceleration: {}",
                     HWDecoderTypeToString(hw_context_->GetType()));
      return Result<void>::Ok();
    }

    // 硬件初始化失败，回退到软件解码
    ZENREMOTE_WARN("Hardware decoder init failed: {}, falling back to software",
                   hw_result.Message());
  }

  // 软件解码
  auto sw_result = InitSWDecoder(config);
  if (sw_result.IsErr()) {
    return sw_result;
  }

  initialized_ = true;
  stats_.hw_accel_active = false;
  ZENREMOTE_INFO("VideoDecoder initialized with software decoding");

  return Result<void>::Ok();
}

Result<void> VideoDecoder::InitHWDecoder(const DecoderConfig& config) {
  // 查找解码器
  codec_ = avcodec_find_decoder(config.codec_id);
  if (!codec_) {
    return Result<void>::Err(ErrorCode::kDecoderNotFound,
                             fmt::format("Decoder not found for codec: {}",
                                         avcodec_get_name(config.codec_id)));
  }

  // 分配解码器上下文
  AVCodecContext* ctx = avcodec_alloc_context3(codec_);
  if (!ctx) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate decoder context");
  }
  codec_ctx_.reset(ctx);

  // 确定硬件解码器类型
  HWDecoderType hw_type = config.hw_decoder_type;
  if (hw_type == HWDecoderType::kNone) {
    hw_type = DetectRecommendedHWDecoder();
    if (hw_type == HWDecoderType::kNone) {
      return Result<void>::Err(ErrorCode::kNotSupported,
                               "No hardware decoder available");
    }
  }

  // 检查硬件解码器可用性
  if (!IsHWDecoderAvailable(hw_type)) {
    return Result<void>::Err(ErrorCode::kNotSupported,
                             fmt::format("Hardware decoder {} not available",
                                         HWDecoderTypeToString(hw_type)));
  }

  // 初始化硬件解码上下文
  hw_context_ = std::make_unique<HWDecoderContext>();

  int width = config.width > 0 ? config.width : 1920;
  int height = config.height > 0 ? config.height : 1080;

  auto hw_init_result =
      hw_context_->Initialize(hw_type, config.codec_id, width, height);
  if (hw_init_result.IsErr()) {
    hw_context_.reset();
    return hw_init_result;
  }

  // 配置解码器上下文
  auto config_result = ConfigureContext(config);
  if (config_result.IsErr()) {
    hw_context_.reset();
    return config_result;
  }

  // 配置解码器使用硬件加速
  auto hw_config_result = hw_context_->ConfigureDecoder(codec_ctx_.get());
  if (hw_config_result.IsErr()) {
    hw_context_.reset();
    return hw_config_result;
  }

  // 打开解码器
  int ret = avcodec_open2(codec_ctx_.get(), codec_, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    hw_context_.reset();
    return Result<void>::Err(ErrorCode::kDecoderInitFailed,
                             fmt::format("Failed to open decoder: {}", errbuf));
  }

  // 分配输入包
  pkt_.reset(av_packet_alloc());
  if (!pkt_) {
    hw_context_.reset();
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate AVPacket");
  }

  return Result<void>::Ok();
}

Result<void> VideoDecoder::InitSWDecoder(const DecoderConfig& config) {
  // 清理可能的旧状态
  hw_context_.reset();
  codec_ctx_.reset();

  // 查找解码器
  codec_ = avcodec_find_decoder(config.codec_id);
  if (!codec_) {
    return Result<void>::Err(ErrorCode::kDecoderNotFound,
                             fmt::format("Decoder not found for codec: {}",
                                         avcodec_get_name(config.codec_id)));
  }

  // 分配解码器上下文
  AVCodecContext* ctx = avcodec_alloc_context3(codec_);
  if (!ctx) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate decoder context");
  }
  codec_ctx_.reset(ctx);

  // 配置解码器上下文
  auto config_result = ConfigureContext(config);
  if (config_result.IsErr()) {
    return config_result;
  }

  // 设置多线程
  if (config.thread_count > 0) {
    codec_ctx_->thread_count = config.thread_count;
  } else {
    codec_ctx_->thread_count = 4;  // 默认 4 线程
  }
  codec_ctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

  // 打开解码器
  int ret = avcodec_open2(codec_ctx_.get(), codec_, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(ErrorCode::kDecoderInitFailed,
                             fmt::format("Failed to open decoder: {}", errbuf));
  }

  // 分配输入包
  pkt_.reset(av_packet_alloc());
  if (!pkt_) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to allocate AVPacket");
  }

  return Result<void>::Ok();
}

Result<void> VideoDecoder::ConfigureContext(const DecoderConfig& config) {
  AVCodecContext* ctx = codec_ctx_.get();

  // 设置额外数据（SPS/PPS等）
  if (!config.extradata.empty()) {
    ctx->extradata_size = static_cast<int>(config.extradata.size());
    ctx->extradata = static_cast<uint8_t*>(
        av_mallocz(ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));

    if (!ctx->extradata) {
      return Result<void>::Err(ErrorCode::kOutOfMemory,
                               "Failed to allocate extradata");
    }

    memcpy(ctx->extradata, config.extradata.data(), ctx->extradata_size);
  }

  // 设置尺寸（如果已知）
  if (config.width > 0) {
    ctx->width = config.width;
  }
  if (config.height > 0) {
    ctx->height = config.height;
  }

  return Result<void>::Ok();
}

void VideoDecoder::Shutdown() {
  if (!initialized_) {
    return;
  }

  pkt_.reset();
  codec_ctx_.reset();
  hw_context_.reset();
  codec_ = nullptr;

  initialized_ = false;
  hw_init_attempted_ = false;

  ZENREMOTE_INFO("VideoDecoder shutdown, decoded {} frames",
                 stats_.frames_decoded);
}

Result<bool> VideoDecoder::Decode(const uint8_t* data,
                                  int size,
                                  int64_t pts,
                                  int64_t dts,
                                  AVFrame* frame) {
  if (!initialized_) {
    return Result<bool>::Err(ErrorCode::kNotInitialized,
                             "Decoder not initialized");
  }

  if (!data || size <= 0) {
    return Result<bool>::Err(ErrorCode::kInvalidParameter,
                             "Invalid input data");
  }

  if (!frame) {
    return Result<bool>::Err(ErrorCode::kInvalidParameter,
                             "Null frame pointer");
  }

  TIMER_START(decode);

  // 设置输入包
  av_packet_unref(pkt_.get());
  pkt_->data = const_cast<uint8_t*>(data);
  pkt_->size = size;
  pkt_->pts = pts;
  pkt_->dts = dts;

  // 发送到解码器
  int ret = avcodec_send_packet(codec_ctx_.get(), pkt_.get());
  if (ret < 0) {
    if (ret == AVERROR(EAGAIN)) {
      // 需要先接收输出
      auto recv_result = ReceiveFrame(frame);
      if (recv_result.IsErr()) {
        return recv_result;
      }

      // 重试发送
      ret = avcodec_send_packet(codec_ctx_.get(), pkt_.get());
      if (ret < 0 && ret != AVERROR_EOF) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        return Result<bool>::Err(
            ErrorCode::kDecodeFailed,
            fmt::format("avcodec_send_packet failed: {}", errbuf));
      }
    } else if (ret != AVERROR_EOF) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      return Result<bool>::Err(
          ErrorCode::kDecodeFailed,
          fmt::format("avcodec_send_packet failed: {}", errbuf));
    }
  }

  // 接收解码输出
  auto recv_result = ReceiveFrame(frame);

  double decode_time = TIMER_END_MS(decode);

  if (recv_result.IsOk() && recv_result.Value()) {
    UpdateStats(size, decode_time);
  }

  return recv_result;
}

Result<bool> VideoDecoder::Decode(const AVPacket* packet, AVFrame* frame) {
  if (!packet) {
    return Result<bool>::Err(ErrorCode::kInvalidParameter,
                             "Null packet pointer");
  }

  return Decode(packet->data, packet->size, packet->pts, packet->dts, frame);
}

Result<bool> VideoDecoder::ReceiveFrame(AVFrame* frame) {
  av_frame_unref(frame);

  int ret = avcodec_receive_frame(codec_ctx_.get(), frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return Result<bool>::Ok(false);  // 暂无输出
  }

  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<bool>::Err(
        ErrorCode::kDecodeFailed,
        fmt::format("avcodec_receive_frame failed: {}", errbuf));
  }

  return Result<bool>::Ok(true);
}

Result<void> VideoDecoder::Flush(std::vector<AVFramePtr>& frames) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Decoder not initialized");
  }

  frames.clear();

  // 发送空包以刷新
  int ret = avcodec_send_packet(codec_ctx_.get(), nullptr);
  if (ret < 0 && ret != AVERROR_EOF) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kDecodeFailed,
        fmt::format("Failed to flush decoder: {}", errbuf));
  }

  // 接收所有剩余输出
  while (true) {
    AVFramePtr frame = MakeAVFrame();
    if (!frame) {
      return Result<void>::Err(ErrorCode::kOutOfMemory,
                               "Failed to allocate frame");
    }

    ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }

    if (ret < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      return Result<void>::Err(
          ErrorCode::kDecodeFailed,
          fmt::format("avcodec_receive_frame failed: {}", errbuf));
    }

    frames.push_back(std::move(frame));
  }

  ZENREMOTE_DEBUG("Decoder flushed, {} frames", frames.size());
  return Result<void>::Ok();
}

void VideoDecoder::FlushBuffers() {
  if (initialized_ && codec_ctx_) {
    avcodec_flush_buffers(codec_ctx_.get());
    ZENREMOTE_DEBUG("Decoder buffers flushed");
  }
}

void VideoDecoder::UpdateStats(int bytes, double decode_time_ms) {
  stats_.frames_decoded++;
  stats_.total_bytes += bytes;

  total_decode_time_ms_ += decode_time_ms;
  stats_.avg_decode_time_ms = total_decode_time_ms_ / stats_.frames_decoded;
}

HWDecoderType VideoDecoder::GetHWDecoderType() const {
  if (hw_context_) {
    return hw_context_->GetType();
  }
  return HWDecoderType::kNone;
}

DecoderStats VideoDecoder::GetStats() const {
  return stats_;
}

int VideoDecoder::GetWidth() const {
  if (codec_ctx_) {
    return codec_ctx_->width;
  }
  return 0;
}

int VideoDecoder::GetHeight() const {
  if (codec_ctx_) {
    return codec_ctx_->height;
  }
  return 0;
}

AVPixelFormat VideoDecoder::GetPixelFormat() const {
  if (codec_ctx_) {
    return codec_ctx_->pix_fmt;
  }
  return AV_PIX_FMT_NONE;
}

std::string VideoDecoder::GetCodecName() const {
  if (codec_) {
    return codec_->name;
  }
  return "unknown";
}

}  // namespace zenremote
