#include "hw_decoder_context.h"

#include "common/log_manager.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#ifdef _WIN32
#include <d3d11.h>
#include <libavutil/hwcontext_d3d11va.h>
#endif

namespace zenremote {

HWDecoderContext::HWDecoderContext() = default;

HWDecoderContext::~HWDecoderContext() {
  Shutdown();
}

Result<void> HWDecoderContext::Initialize(HWDecoderType type,
                                          AVCodecID codec_id,
                                          int width,
                                          int height) {
  if (hw_device_ctx_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "HWDecoderContext already initialized");
  }

  if (type == HWDecoderType::kNone) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid hardware decoder type");
  }

  hw_type_ = type;
  codec_id_ = codec_id;
  hw_pix_fmt_ = ::zenremote::GetHWPixelFormat(type);
  width_ = width;
  height_ = height;

  // 仅创建硬件设备上下文
  // hw_frames_ctx 将在 GetHWFormat 回调中通过 FFmpeg API 动态创建
  auto device_result = CreateDeviceContext();
  if (device_result.IsErr()) {
    return device_result;
  }

#ifdef _WIN32
  // 提取 D3D11 设备指针
  if (hw_type_ == HWDecoderType::kD3D11VA) {
    ExtractD3D11Device();
  }
#endif

  ZENREMOTE_INFO(
      "HWDecoderContext initialized: type={}, codec={}, {}x{} "
      "(hw_frames_ctx will be created by FFmpeg)",
      HWDecoderTypeToString(hw_type_), avcodec_get_name(codec_id_), width_,
      height_);

  return Result<void>::Ok();
}

void HWDecoderContext::Shutdown() {
  // 注意：我们不拥有 hw_frames_ctx，它由 FFmpeg/解码器管理
  last_hw_frames_ctx_ = nullptr;

  if (hw_device_ctx_) {
    av_buffer_unref(&hw_device_ctx_);
    hw_device_ctx_ = nullptr;
  }

#ifdef _WIN32
  d3d11_device_ = nullptr;
  d3d11_device_context_ = nullptr;
#endif

  hw_type_ = HWDecoderType::kNone;

  ZENREMOTE_DEBUG("HWDecoderContext shutdown");
}

Result<void> HWDecoderContext::CreateDeviceContext() {
  AVHWDeviceType av_hw_type = GetAVHWDeviceType(hw_type_);
  if (av_hw_type == AV_HWDEVICE_TYPE_NONE) {
    return Result<void>::Err(ErrorCode::kNotSupported,
                             "Unsupported hardware decoder type");
  }

  int ret =
      av_hwdevice_ctx_create(&hw_device_ctx_, av_hw_type, nullptr, nullptr, 0);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kDecoderInitFailed,
        fmt::format("Failed to create hardware device context: {}", errbuf));
  }

  return Result<void>::Ok();
}

Result<void> HWDecoderContext::ConfigureDecoder(AVCodecContext* codec_ctx) {
  if (!hw_device_ctx_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "HWDecoderContext not initialized");
  }

  if (!codec_ctx) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Null codec context");
  }

  // 设置硬件设备上下文
  codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
  if (!codec_ctx->hw_device_ctx) {
    return Result<void>::Err(ErrorCode::kOutOfMemory,
                             "Failed to reference hardware device context");
  }

  // 设置 get_format 回调以协商硬件格式
  // 将 this 指针存储到 opaque 中供回调使用
  codec_ctx->opaque = this;
  codec_ctx->get_format = GetHWFormat;

  // 硬件解码通常使用单线程
  codec_ctx->thread_count = 1;

  // 关键：不要在这里创建 hw_frames_ctx
  // 在 FFmpeg 的 get_format 回调中通过 avcodec_get_hw_frames_parameters 创建

  ZENREMOTE_DEBUG(
      "Decoder configured for hardware acceleration "
      "(hw_frames_ctx will be created by FFmpeg in get_format)");

  return Result<void>::Ok();
}

#ifdef _WIN32
void HWDecoderContext::ExtractD3D11Device() {
  if (!hw_device_ctx_ || hw_type_ != HWDecoderType::kD3D11VA) {
    return;
  }

  AVHWDeviceContext* device_ctx =
      reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx_->data);

  if (device_ctx->type == AV_HWDEVICE_TYPE_D3D11VA) {
    AVD3D11VADeviceContext* d3d11_ctx =
        static_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);

    d3d11_device_ = d3d11_ctx->device;
    d3d11_device_context_ = d3d11_ctx->device_context;

    ZENREMOTE_DEBUG("D3D11 device extracted: device={}, context={}",
                    static_cast<void*>(d3d11_device_),
                    static_cast<void*>(d3d11_device_context_));
  }
}

bool HWDecoderContext::EnsureD3D11BindFlags(AVBufferRef* frames_ctx_ref) {
  if (!frames_ctx_ref || hw_type_ != HWDecoderType::kD3D11VA) {
    return true;  // 非 D3D11VA 不需要处理
  }

  AVHWFramesContext* frames_ctx =
      reinterpret_cast<AVHWFramesContext*>(frames_ctx_ref->data);

  if (frames_ctx->format != AV_PIX_FMT_D3D11) {
    return true;  // 非 D3D11 格式不需要处理
  }

  AVD3D11VAFramesContext* d3d11_frames_ctx =
      static_cast<AVD3D11VAFramesContext*>(frames_ctx->hwctx);

  // 添加 SHADER_RESOURCE flag（零拷贝渲染必需的）
  // 保留 FFmpeg 设置的其他 flags
  d3d11_frames_ctx->BindFlags |= D3D11_BIND_SHADER_RESOURCE;

  ZENREMOTE_DEBUG("D3D11 BindFlags updated to 0x{:X} (added SHADER_RESOURCE)",
                  d3d11_frames_ctx->BindFlags);

  return true;
}

ID3D11Texture2D* HWDecoderContext::GetD3D11Texture(AVFrame* frame) const {
  if (!frame || frame->format != AV_PIX_FMT_D3D11) {
    ZENREMOTE_ERROR("Invalid frame format for D3D11 texture extraction");
    return nullptr;
  }

  // AVFrame::data[0] 存储的是 ID3D11Texture2D*
  // AVFrame::data[1] 存储的是纹理数组索引
  return reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
}
#endif

// ============================================================================
// GetHWFormat - 核心硬件格式协商回调（参考 zenplay/MPV 实现）
// ============================================================================
AVPixelFormat HWDecoderContext::GetHWFormat(AVCodecContext* ctx,
                                            const AVPixelFormat* pix_fmts) {
  HWDecoderContext* hw_ctx = static_cast<HWDecoderContext*>(ctx->opaque);
  if (!hw_ctx) {
    ZENREMOTE_ERROR("Invalid opaque pointer in GetHWFormat");
    return AV_PIX_FMT_NONE;
  }

  // 查找支持的硬件格式
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    if (*p == hw_ctx->hw_pix_fmt_) {
      ZENREMOTE_DEBUG("Found target HW pixel format: {}",
                      av_get_pix_fmt_name(*p));

      // ========== 关键：像 MPV 一样，在 get_format 中创建 hw_frames_ctx
      // ==========
      AVBufferRef* current_frames_ctx = ctx->hw_frames_ctx;

      if (current_frames_ctx == nullptr) {
        // 首次：使用 FFmpeg API 创建 hw_frames_ctx
        ZENREMOTE_INFO("Creating hw_frames_ctx via FFmpeg API (MPV-style)");

        auto result = hw_ctx->InitGenericHWAccel(ctx, *p);
        if (!result.IsOk()) {
          ZENREMOTE_ERROR(
              "Failed to init hw_frames_ctx: {}, falling back to SW",
              result.Message());
          return AV_PIX_FMT_NONE;  // 回退软件解码
        }

        current_frames_ctx = ctx->hw_frames_ctx;
      } else if (current_frames_ctx != hw_ctx->last_hw_frames_ctx_) {
        // 检测到 FFmpeg 替换了帧上下文（例如分辨率变化）
        ZENREMOTE_INFO("Detected new hw_frames_ctx from FFmpeg, reconfiguring");

        av_buffer_unref(&ctx->hw_frames_ctx);
        hw_ctx->last_hw_frames_ctx_ = nullptr;

        auto result = hw_ctx->InitGenericHWAccel(ctx, *p);
        if (!result.IsOk()) {
          ZENREMOTE_ERROR(
              "Failed to reinit hw_frames_ctx: {}, falling back to SW",
              result.Message());
          return AV_PIX_FMT_NONE;
        }

        current_frames_ctx = ctx->hw_frames_ctx;
      }

#ifdef _WIN32
      // D3D11 零拷贝支持：确保 BindFlags 正确
      if (!hw_ctx->EnsureD3D11BindFlags(current_frames_ctx)) {
        ZENREMOTE_ERROR("Failed to ensure D3D11 BindFlags, falling back to SW");
        if (ctx->hw_frames_ctx) {
          av_buffer_unref(&ctx->hw_frames_ctx);
        }
        hw_ctx->last_hw_frames_ctx_ = nullptr;
        return AV_PIX_FMT_NONE;
      }
#endif

      ZENREMOTE_INFO("Selected hardware pixel format: {}",
                     av_get_pix_fmt_name(*p));
      return *p;
    }
  }

  // 未找到目标硬件格式
  ZENREMOTE_WARN("Target HW format {} not in available formats, falling back",
                 av_get_pix_fmt_name(hw_ctx->hw_pix_fmt_));

  // 打印可用格式供调试
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
    ZENREMOTE_DEBUG("  Available format: {}", av_get_pix_fmt_name(*p));
  }

  return AV_PIX_FMT_NONE;  // 回退软件解码
}

// ============================================================================
// InitGenericHWAccel - 使用 FFmpeg API 创建 hw_frames_ctx（参考 zenplay/MPV）
// ============================================================================
Result<void> HWDecoderContext::InitGenericHWAccel(AVCodecContext* ctx,
                                                  AVPixelFormat hw_fmt) {
  ZENREMOTE_DEBUG("Initializing generic hwaccel (MPV-style) for format: {}",
                  av_get_pix_fmt_name(hw_fmt));

  // ========== 关键：使用 FFmpeg API 创建 hw_frames_ctx ==========
  // 这比手动创建更好，因为 FFmpeg 会计算正确的池大小
  AVBufferRef* new_frames_ctx = nullptr;
  int ret = avcodec_get_hw_frames_parameters(ctx, hw_device_ctx_, hw_fmt,
                                             &new_frames_ctx);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return Result<void>::Err(
        ErrorCode::kDecoderInitFailed,
        fmt::format("avcodec_get_hw_frames_parameters failed: {} "
                    "(codec may not support hardware decoding)",
                    errbuf));
  }

  AVHWFramesContext* frames_ctx =
      reinterpret_cast<AVHWFramesContext*>(new_frames_ctx->data);

  ZENREMOTE_DEBUG(
      "FFmpeg calculated frames context: format={}, sw_format={}, "
      "{}x{}, initial_pool_size={}",
      av_get_pix_fmt_name(frames_ctx->format),
      av_get_pix_fmt_name(frames_ctx->sw_format), frames_ctx->width,
      frames_ctx->height, frames_ctx->initial_pool_size);

  // ========== 调整池大小（参考 MPV hwdec_extra_frames）=========
  // FFmpeg 已经计算了基础池大小，我们需要加上额外的缓冲
  //
  // 为什么需要额外缓冲？
  // - FFmpeg 基础需 6-8 个 Surface（DPB 需求）
  // - 应用层队列缓冲: 额外需要一些帧
  // - 推荐: initial_pool_size + 6-8 个额外帧以防止阻塞
  int extra_frames = 6;
  frames_ctx->initial_pool_size += extra_frames;

  ZENREMOTE_DEBUG("Adjusted pool size to {} (+{} extra frames)",
                  frames_ctx->initial_pool_size, extra_frames);

#ifdef _WIN32
  // D3D11 特定：设置 BindFlags 以支持零拷贝
  if (hw_type_ == HWDecoderType::kD3D11VA) {
    AVD3D11VAFramesContext* d3d11_frames_ctx =
        reinterpret_cast<AVD3D11VAFramesContext*>(frames_ctx->hwctx);

    // 添加 SHADER_RESOURCE flag（保留 FFmpeg 设置的其他 flags）
    d3d11_frames_ctx->BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    ZENREMOTE_DEBUG("D3D11: Added SHADER_RESOURCE flag, BindFlags=0x{:X}",
                    d3d11_frames_ctx->BindFlags);
  }
#endif

  // 初始化帧上下文
  ret = av_hwframe_ctx_init(new_frames_ctx);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    av_buffer_unref(&new_frames_ctx);
    return Result<void>::Err(
        ErrorCode::kDecoderInitFailed,
        fmt::format("av_hwframe_ctx_init failed: {}", errbuf));
  }

  // 赋值给解码器（FFmpeg 接管所有权）
  ctx->hw_frames_ctx = new_frames_ctx;
  last_hw_frames_ctx_ = new_frames_ctx;

  ZENREMOTE_INFO("hw_frames_ctx initialized successfully via FFmpeg API");

  return Result<void>::Ok();
}

bool HWDecoderContext::ValidateFramesContext(AVCodecContext* codec_ctx) const {
  if (!codec_ctx || !codec_ctx->hw_frames_ctx) {
    ZENREMOTE_WARN("No hw_frames_ctx to validate");
    return false;
  }

  AVHWFramesContext* frames_ctx =
      reinterpret_cast<AVHWFramesContext*>(codec_ctx->hw_frames_ctx->data);

  ZENREMOTE_INFO("Validating frames context: format={}, sw_format={}, {}x{}",
                 av_get_pix_fmt_name(frames_ctx->format),
                 av_get_pix_fmt_name(frames_ctx->sw_format), frames_ctx->width,
                 frames_ctx->height);

#ifdef _WIN32
  if (hw_type_ == HWDecoderType::kD3D11VA) {
    AVD3D11VAFramesContext* d3d11_ctx =
        reinterpret_cast<AVD3D11VAFramesContext*>(frames_ctx->hwctx);

    bool has_shader_resource =
        (d3d11_ctx->BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;

    ZENREMOTE_INFO("D3D11 BindFlags=0x{:X}, SHADER_RESOURCE={}",
                   d3d11_ctx->BindFlags,
                   has_shader_resource ? "yes" : "NO (zero-copy disabled!)");

    return has_shader_resource;
  }
#endif

  return true;
}

}  // namespace zenremote
