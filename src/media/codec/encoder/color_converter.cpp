#include "color_converter.h"

#include "common/log_manager.h"

namespace zenremote {

ColorConverter::~ColorConverter() {
  Shutdown();
}

ColorConverter::ColorConverter(ColorConverter&& other) noexcept
    : sws_ctx_(other.sws_ctx_),
      src_width_(other.src_width_),
      src_height_(other.src_height_),
      src_format_(other.src_format_),
      dst_width_(other.dst_width_),
      dst_height_(other.dst_height_),
      dst_format_(other.dst_format_),
      sws_flags_(other.sws_flags_) {
  other.sws_ctx_ = nullptr;
}

ColorConverter& ColorConverter::operator=(ColorConverter&& other) noexcept {
  if (this != &other) {
    Shutdown();
    sws_ctx_ = other.sws_ctx_;
    src_width_ = other.src_width_;
    src_height_ = other.src_height_;
    src_format_ = other.src_format_;
    dst_width_ = other.dst_width_;
    dst_height_ = other.dst_height_;
    dst_format_ = other.dst_format_;
    sws_flags_ = other.sws_flags_;
    other.sws_ctx_ = nullptr;
  }
  return *this;
}

Result<void> ColorConverter::Initialize(const ColorConverterConfig& config) {
  if (sws_ctx_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "ColorConverter already initialized");
  }

  if (config.src_width <= 0 || config.src_height <= 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid source dimensions");
  }

  if (config.src_format == AV_PIX_FMT_NONE) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid source pixel format");
  }

  src_width_ = config.src_width;
  src_height_ = config.src_height;
  src_format_ = config.src_format;

  // 目标尺寸，默认与源相同
  dst_width_ = (config.dst_width > 0) ? config.dst_width : src_width_;
  dst_height_ = (config.dst_height > 0) ? config.dst_height : src_height_;
  dst_format_ = (config.dst_format != AV_PIX_FMT_NONE) ? config.dst_format
                                                       : AV_PIX_FMT_NV12;
  sws_flags_ = config.sws_flags;

  // 创建 swscale 上下文
  sws_ctx_ = sws_getContext(src_width_, src_height_, src_format_, dst_width_,
                            dst_height_, dst_format_, sws_flags_, nullptr,
                            nullptr, nullptr);

  if (!sws_ctx_) {
    return Result<void>::Err(ErrorCode::kCodecError,
                             "Failed to create swscale context");
  }

  ZENREMOTE_INFO("ColorConverter initialized: {}x{} ({}) -> {}x{} ({})",
                 src_width_, src_height_, av_get_pix_fmt_name(src_format_),
                 dst_width_, dst_height_, av_get_pix_fmt_name(dst_format_));

  return Result<void>::Ok();
}

void ColorConverter::Shutdown() {
  if (sws_ctx_) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
    ZENREMOTE_DEBUG("ColorConverter shutdown");
  }
}

Result<void> ColorConverter::Convert(const AVFrame* src_frame,
                                     AVFrame* dst_frame) {
  if (!sws_ctx_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "ColorConverter not initialized");
  }

  if (!src_frame || !dst_frame) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Null frame pointer");
  }

  // 验证源帧尺寸
  if (src_frame->width != src_width_ || src_frame->height != src_height_) {
    return Result<void>::Err(
        ErrorCode::kInvalidParameter,
        fmt::format("Source frame size mismatch: expected {}x{}, got {}x{}",
                    src_width_, src_height_, src_frame->width,
                    src_frame->height));
  }

  // 设置目标帧属性
  dst_frame->width = dst_width_;
  dst_frame->height = dst_height_;
  dst_frame->format = static_cast<int>(dst_format_);

  // 执行转换
  int result = sws_scale(sws_ctx_, src_frame->data, src_frame->linesize, 0,
                         src_height_, dst_frame->data, dst_frame->linesize);

  if (result <= 0) {
    return Result<void>::Err(ErrorCode::kCodecError, "sws_scale failed");
  }

  // 复制时间戳
  dst_frame->pts = src_frame->pts;
  dst_frame->pkt_dts = src_frame->pkt_dts;

  return Result<void>::Ok();
}

Result<AVFramePtr> ColorConverter::Convert(const AVFrame* src_frame) {
  if (!sws_ctx_) {
    return Result<AVFramePtr>::Err(ErrorCode::kNotInitialized,
                                   "ColorConverter not initialized");
  }

  if (!src_frame) {
    return Result<AVFramePtr>::Err(ErrorCode::kInvalidParameter,
                                   "Null source frame");
  }

  // 创建目标帧
  AVFramePtr dst_frame = MakeAVFrame();
  if (!dst_frame) {
    return Result<AVFramePtr>::Err(ErrorCode::kOutOfMemory,
                                   "Failed to allocate destination frame");
  }

  // 分配 buffer
  auto alloc_result = AllocateDstFrame(dst_frame.get());
  if (alloc_result.IsErr()) {
    return Result<AVFramePtr>::Err(alloc_result.Code(), alloc_result.Message());
  }

  // 执行转换
  auto convert_result = Convert(src_frame, dst_frame.get());
  if (convert_result.IsErr()) {
    return Result<AVFramePtr>::Err(convert_result.Code(),
                                   convert_result.Message());
  }

  return Result<AVFramePtr>::Ok(std::move(dst_frame));
}

Result<void> ColorConverter::Convert(const uint8_t* const* src_data,
                                     const int* src_linesize,
                                     AVFrame* dst_frame) {
  if (!sws_ctx_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "ColorConverter not initialized");
  }

  if (!src_data || !src_linesize || !dst_frame) {
    return Result<void>::Err(ErrorCode::kInvalidParameter, "Null pointer");
  }

  // 设置目标帧属性
  dst_frame->width = dst_width_;
  dst_frame->height = dst_height_;
  dst_frame->format = static_cast<int>(dst_format_);

  // 执行转换
  int result = sws_scale(sws_ctx_, src_data, src_linesize, 0, src_height_,
                         dst_frame->data, dst_frame->linesize);

  if (result <= 0) {
    return Result<void>::Err(ErrorCode::kCodecError, "sws_scale failed");
  }

  return Result<void>::Ok();
}

Result<void> ColorConverter::AllocateDstFrame(AVFrame* frame) const {
  if (!frame) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Null frame pointer");
  }

  frame->format = static_cast<int>(dst_format_);
  frame->width = dst_width_;
  frame->height = dst_height_;

  int ret = av_frame_get_buffer(frame, 32);  // 32字节对齐
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kOutOfMemory,
        fmt::format("av_frame_get_buffer failed: {}", errbuf));
  }

  return Result<void>::Ok();
}

}  // namespace zenremote
