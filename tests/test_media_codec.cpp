#include <gtest/gtest.h>

#include "../src/media/codec/decoder/hw_decoder_type.h"
#include "../src/media/codec/decoder/video_decoder.h"
#include "../src/media/codec/encoder/color_converter.h"
#include "../src/media/codec/encoder/hw_encoder_type.h"
#include "../src/media/codec/encoder/video_encoder.h"
#include "../src/media/renderer/video_renderer.h"

using namespace zenremote;

// ======================== 编码器类型测试 ========================

TEST(HWEncoderTypeTest, EncoderTypeToString) {
  EXPECT_STREQ(HWEncoderTypeToString(HWEncoderType::kNone), "None (Software)");
  EXPECT_STREQ(HWEncoderTypeToString(HWEncoderType::kNVENC), "NVENC");
  EXPECT_STREQ(HWEncoderTypeToString(HWEncoderType::kQSV), "QSV");
  EXPECT_STREQ(HWEncoderTypeToString(HWEncoderType::kAMF), "AMF");
}

TEST(HWEncoderTypeTest, GetHWEncoderName) {
  // H.264 编码器名称
  EXPECT_STREQ(GetHWEncoderName(HWEncoderType::kNVENC, AV_CODEC_ID_H264),
               "h264_nvenc");
  EXPECT_STREQ(GetHWEncoderName(HWEncoderType::kQSV, AV_CODEC_ID_H264),
               "h264_qsv");
  EXPECT_STREQ(GetHWEncoderName(HWEncoderType::kAMF, AV_CODEC_ID_H264),
               "h264_amf");

  // HEVC 编码器名称
  EXPECT_STREQ(GetHWEncoderName(HWEncoderType::kNVENC, AV_CODEC_ID_HEVC),
               "hevc_nvenc");

  // 软件编码器返回 nullptr
  EXPECT_EQ(GetHWEncoderName(HWEncoderType::kNone, AV_CODEC_ID_H264), nullptr);
}

// ======================== 解码器类型测试 ========================

TEST(HWDecoderTypeTest, DecoderTypeToString) {
  EXPECT_STREQ(HWDecoderTypeToString(HWDecoderType::kNone), "None (Software)");
  EXPECT_STREQ(HWDecoderTypeToString(HWDecoderType::kD3D11VA), "D3D11VA");
  EXPECT_STREQ(HWDecoderTypeToString(HWDecoderType::kDXVA2), "DXVA2");
  EXPECT_STREQ(HWDecoderTypeToString(HWDecoderType::kCUDA), "CUDA");
}

TEST(HWDecoderTypeTest, GetAVHWDeviceType) {
  EXPECT_EQ(GetAVHWDeviceType(HWDecoderType::kD3D11VA),
            AV_HWDEVICE_TYPE_D3D11VA);
  EXPECT_EQ(GetAVHWDeviceType(HWDecoderType::kDXVA2), AV_HWDEVICE_TYPE_DXVA2);
  EXPECT_EQ(GetAVHWDeviceType(HWDecoderType::kCUDA), AV_HWDEVICE_TYPE_CUDA);
  EXPECT_EQ(GetAVHWDeviceType(HWDecoderType::kNone), AV_HWDEVICE_TYPE_NONE);
}

TEST(HWDecoderTypeTest, GetHWPixelFormat) {
  EXPECT_EQ(GetHWPixelFormat(HWDecoderType::kD3D11VA), AV_PIX_FMT_D3D11);
  EXPECT_EQ(GetHWPixelFormat(HWDecoderType::kDXVA2), AV_PIX_FMT_DXVA2_VLD);
  EXPECT_EQ(GetHWPixelFormat(HWDecoderType::kCUDA), AV_PIX_FMT_CUDA);
  EXPECT_EQ(GetHWPixelFormat(HWDecoderType::kNone), AV_PIX_FMT_NONE);
}

// ======================== 编码器配置测试 ========================

TEST(EncoderConfigTest, DefaultValues) {
  EncoderConfig config;

  EXPECT_EQ(config.width, 1920);
  EXPECT_EQ(config.height, 1080);
  EXPECT_EQ(config.framerate, 60);
  EXPECT_EQ(config.encoder_type, EncoderType::kSoftware);
  EXPECT_EQ(config.codec_id, AV_CODEC_ID_H264);
  EXPECT_EQ(config.rate_control, RateControlMode::kVBR);
  EXPECT_EQ(config.bitrate, 8000000);
  EXPECT_EQ(config.preset, EncoderPreset::kLowLatency);
  EXPECT_EQ(config.gop_size, 120);
  EXPECT_EQ(config.max_b_frames, 0);
  EXPECT_TRUE(config.zero_latency);
}

TEST(EncoderConfigTest, PresetToString) {
  EXPECT_STREQ(EncoderPresetToString(EncoderPreset::kUltrafast), "ultrafast");
  EXPECT_STREQ(EncoderPresetToString(EncoderPreset::kMedium), "medium");
  EXPECT_STREQ(EncoderPresetToString(EncoderPreset::kSlow), "slow");
  EXPECT_STREQ(EncoderPresetToString(EncoderPreset::kLowLatency), "ultrafast");
}

// ======================== 解码器配置测试 ========================

TEST(DecoderConfigTest, DefaultValues) {
  DecoderConfig config;

  EXPECT_EQ(config.codec_id, AV_CODEC_ID_H264);
  EXPECT_TRUE(config.use_hw_decoder);
  EXPECT_EQ(config.hw_decoder_type, HWDecoderType::kNone);  // 自动检测
  EXPECT_EQ(config.width, 0);
  EXPECT_EQ(config.height, 0);
}

// ======================== 渲染器类型测试 ========================

TEST(RendererTypeTest, TypeToString) {
  EXPECT_STREQ(RendererTypeToString(RendererType::kSDL), "SDL");
  EXPECT_STREQ(RendererTypeToString(RendererType::kD3D11), "D3D11");
  EXPECT_STREQ(RendererTypeToString(RendererType::kOpenGL), "OpenGL");
}

TEST(RendererConfigTest, DefaultValues) {
  RendererConfig config;

  EXPECT_EQ(config.window_handle, nullptr);
  EXPECT_EQ(config.width, 1920);
  EXPECT_EQ(config.height, 1080);
  EXPECT_EQ(config.input_format, AV_PIX_FMT_NV12);
  EXPECT_EQ(config.renderer_type, RendererType::kSDL);
  EXPECT_TRUE(config.vsync);
}

// ======================== 色彩转换器配置测试 ========================

TEST(ColorConverterConfigTest, DefaultValues) {
  ColorConverterConfig config;

  EXPECT_EQ(config.src_format, AV_PIX_FMT_BGRA);
  EXPECT_EQ(config.dst_format, AV_PIX_FMT_NV12);
  EXPECT_EQ(config.sws_flags, SWS_BILINEAR);
}

// ======================== 工厂函数测试 ========================

TEST(CreateVideoEncoderTest, SoftwareEncoder) {
  EncoderConfig config;
  config.width = 640;
  config.height = 480;
  config.framerate = 30;
  config.encoder_type = EncoderType::kSoftware;
  config.bitrate = 2000000;

  auto result = CreateVideoEncoder(config);

  // 如果 libx264 可用，应该成功
  if (result.IsOk()) {
    auto& encoder = result.Value();
    EXPECT_TRUE(encoder->IsInitialized());
    EXPECT_EQ(encoder->GetEncoderType(), EncoderType::kSoftware);
    EXPECT_EQ(encoder->GetEncoderName(), "libx264");
    encoder->Shutdown();
  }
}

TEST(CreateVideoRendererTest, SDLRenderer) {
  auto result = CreateVideoRenderer(RendererType::kSDL);

  EXPECT_TRUE(result.IsOk());
  if (result.IsOk()) {
    auto& renderer = result.Value();
    EXPECT_EQ(renderer->GetType(), RendererType::kSDL);
    EXPECT_EQ(renderer->GetName(), "SDL2 Renderer");
    EXPECT_FALSE(renderer->SupportsZeroCopy());
    EXPECT_FALSE(renderer->IsInitialized());  // 未调用 Initialize
  }
}

#ifdef _WIN32
TEST(CreateVideoRendererTest, D3D11Renderer) {
  auto result = CreateVideoRenderer(RendererType::kD3D11);

  EXPECT_TRUE(result.IsOk());
  if (result.IsOk()) {
    auto& renderer = result.Value();
    EXPECT_EQ(renderer->GetType(), RendererType::kD3D11);
    EXPECT_EQ(renderer->GetName(), "D3D11 Renderer");
    EXPECT_FALSE(renderer->IsInitialized());  // 未调用 Initialize
  }
}
#endif

TEST(CreateVideoRendererTest, OpenGLNotImplemented) {
  auto result = CreateVideoRenderer(RendererType::kOpenGL);

  EXPECT_TRUE(result.IsErr());
  EXPECT_EQ(result.Code(), ErrorCode::kNotImplemented);
}

// ======================== 统计信息测试 ========================

TEST(EncoderStatsTest, DefaultValues) {
  EncoderStats stats;

  EXPECT_EQ(stats.frames_encoded, 0);
  EXPECT_EQ(stats.keyframes_encoded, 0);
  EXPECT_DOUBLE_EQ(stats.avg_encode_time_ms, 0);
  EXPECT_DOUBLE_EQ(stats.avg_bitrate, 0);
  EXPECT_EQ(stats.total_bytes, 0);
}

TEST(DecoderStatsTest, DefaultValues) {
  DecoderStats stats;

  EXPECT_EQ(stats.frames_decoded, 0);
  EXPECT_EQ(stats.keyframes_decoded, 0);
  EXPECT_DOUBLE_EQ(stats.avg_decode_time_ms, 0);
  EXPECT_EQ(stats.total_bytes, 0);
  EXPECT_FALSE(stats.hw_accel_active);
}

TEST(RenderStatsTest, DefaultValues) {
  RenderStats stats;

  EXPECT_EQ(stats.frames_rendered, 0);
  EXPECT_EQ(stats.frames_dropped, 0);
  EXPECT_DOUBLE_EQ(stats.avg_render_time_ms, 0);
  EXPECT_DOUBLE_EQ(stats.fps, 0);
}
