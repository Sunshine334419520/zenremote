#include <iomanip>
#include <iostream>

#include "../src/media/codec/decoder/hw_decoder_type.h"
#include "../src/media/codec/decoder/video_decoder.h"
#include "../src/media/codec/encoder/hw_encoder_type.h"
#include "../src/media/codec/encoder/video_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace zenremote;

void PrintSeparator(const std::string& title) {
  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "  " << title << std::endl;
  std::cout << std::string(60, '=') << std::endl;
}

void PrintStatus(const std::string& name, bool available) {
  std::cout << std::left << std::setw(30) << name << ": ";
  if (available) {
    std::cout << "\033[32m✓ Available\033[0m" << std::endl;
  } else {
    std::cout << "\033[31m✗ Not Available\033[0m" << std::endl;
  }
}

void CheckHardwareEncoders() {
  PrintSeparator("Hardware Encoder Detection");

  // NVIDIA NVENC
  bool nvenc_h264 =
      IsHWEncoderAvailable(HWEncoderType::kNVENC, AV_CODEC_ID_H264);
  PrintStatus("NVENC H.264", nvenc_h264);

  bool nvenc_hevc =
      IsHWEncoderAvailable(HWEncoderType::kNVENC, AV_CODEC_ID_HEVC);
  PrintStatus("NVENC HEVC", nvenc_hevc);

  // Intel QSV
  bool qsv_h264 = IsHWEncoderAvailable(HWEncoderType::kQSV, AV_CODEC_ID_H264);
  PrintStatus("Intel QSV H.264", qsv_h264);

  bool qsv_hevc = IsHWEncoderAvailable(HWEncoderType::kQSV, AV_CODEC_ID_HEVC);
  PrintStatus("Intel QSV HEVC", qsv_hevc);

  // AMD AMF
  bool amf_h264 = IsHWEncoderAvailable(HWEncoderType::kAMF, AV_CODEC_ID_H264);
  PrintStatus("AMD AMF H.264", amf_h264);

  bool amf_hevc = IsHWEncoderAvailable(HWEncoderType::kAMF, AV_CODEC_ID_HEVC);
  PrintStatus("AMD AMF HEVC", amf_hevc);

  // 软件编码器
  const AVCodec* libx264 = avcodec_find_encoder_by_name("libx264");
  PrintStatus("libx264 (Software)", libx264 != nullptr);

  const AVCodec* libx265 = avcodec_find_encoder_by_name("libx265");
  PrintStatus("libx265 (Software)", libx265 != nullptr);

  // 自动检测
  std::cout << "\n\033[36mAuto-detected encoder: \033[0m";
  HWEncoderType detected = DetectAvailableHWEncoder(AV_CODEC_ID_H264);
  std::cout << HWEncoderTypeToString(detected) << std::endl;
}

void CheckHardwareDecoders() {
  PrintSeparator("Hardware Decoder Detection");

  // Windows
  bool d3d11va = IsHWDecoderAvailable(HWDecoderType::kD3D11VA);
  PrintStatus("D3D11VA", d3d11va);

  bool dxva2 = IsHWDecoderAvailable(HWDecoderType::kDXVA2);
  PrintStatus("DXVA2", dxva2);

  // NVIDIA
  bool cuda = IsHWDecoderAvailable(HWDecoderType::kCUDA);
  PrintStatus("NVIDIA CUDA", cuda);

  // Intel
  bool qsv = IsHWDecoderAvailable(HWDecoderType::kQSV);
  PrintStatus("Intel QSV Decode", qsv);

  // 自动检测
  std::cout << "\n\033[36mRecommended decoder: \033[0m";
  HWDecoderType detected = DetectRecommendedHWDecoder();
  std::cout << HWDecoderTypeToString(detected) << std::endl;
}

void CheckFFmpegConfiguration() {
  PrintSeparator("FFmpeg Configuration");

  std::cout << "FFmpeg Version: " << av_version_info() << std::endl;
  std::cout << "Build Configuration:\n" << avcodec_configuration() << std::endl;
}

void ListAvailableEncoders() {
  PrintSeparator("All Available Video Encoders");

  const AVCodec* codec = nullptr;
  void* iter = nullptr;
  int count = 0;

  while ((codec = av_codec_iterate(&iter))) {
    if (av_codec_is_encoder(codec) && codec->type == AVMEDIA_TYPE_VIDEO) {
      std::cout << std::left << std::setw(25) << codec->name;
      std::cout << " - " << codec->long_name << std::endl;
      count++;
    }
  }

  std::cout << "\nTotal: " << count << " encoders" << std::endl;
}

void ListAvailableDecoders() {
  PrintSeparator("All Available Video Decoders");

  const AVCodec* codec = nullptr;
  void* iter = nullptr;
  int count = 0;

  while ((codec = av_codec_iterate(&iter))) {
    if (av_codec_is_decoder(codec) && codec->type == AVMEDIA_TYPE_VIDEO) {
      std::cout << std::left << std::setw(25) << codec->name;
      std::cout << " - " << codec->long_name << std::endl;
      count++;
    }
  }

  std::cout << "\nTotal: " << count << " decoders" << std::endl;
}

void CheckHWAccelMethods() {
  PrintSeparator("Hardware Acceleration Methods");

  enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
  int count = 0;

  while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
    const char* name = av_hwdevice_get_type_name(type);
    std::cout << "  • " << name << std::endl;
    count++;
  }

  if (count == 0) {
    std::cout
        << "\033[31m✗ No hardware acceleration support compiled in!\033[0m"
        << std::endl;
    std::cout << "  This means FFmpeg was built without --enable-d3d11va, "
                 "--enable-cuda, etc."
              << std::endl;
  } else {
    std::cout << "\nTotal: " << count << " hardware acceleration methods"
              << std::endl;
  }
}

void PerformanceEstimate() {
  PrintSeparator("Performance Estimate");

  std::cout
      << "Based on detected hardware, estimated performance for 1080p60:\n"
      << std::endl;

  // 检测编码器
  HWEncoderType encoder = DetectAvailableHWEncoder(AV_CODEC_ID_H264);
  if (encoder != HWEncoderType::kNone) {
    std::cout << "Encoding (" << HWEncoderTypeToString(encoder)
              << "):" << std::endl;
    std::cout << "  CPU Usage:  ~5-10%" << std::endl;
    std::cout << "  Latency:    1-2 frames (16-33ms)" << std::endl;
    std::cout << "  Quality:    ★★★★☆" << std::endl;
  } else {
    std::cout << "Encoding (libx264 software):" << std::endl;
    std::cout << "  CPU Usage:  ~60-80%" << std::endl;
    std::cout << "  Latency:    3-5 frames (50-83ms)" << std::endl;
    std::cout << "  Quality:    ★★★★★" << std::endl;
  }

  std::cout << std::endl;

  // 检测解码器
  HWDecoderType decoder = DetectRecommendedHWDecoder();
  if (decoder != HWDecoderType::kNone) {
    std::cout << "Decoding (" << HWDecoderTypeToString(decoder)
              << "):" << std::endl;
    std::cout << "  CPU Usage:  ~2-5%" << std::endl;
    std::cout << "  GPU Usage:  ~5-10%" << std::endl;
    std::cout << "  Zero-copy:  Available with D3D11Renderer" << std::endl;
  } else {
    std::cout << "Decoding (software):" << std::endl;
    std::cout << "  CPU Usage:  ~40-50%" << std::endl;
    std::cout << "  GPU Usage:  0%" << std::endl;
    std::cout << "  Zero-copy:  Not available" << std::endl;
  }
}

void RecommendConfiguration() {
  PrintSeparator("Recommended Configuration");

  HWEncoderType encoder = DetectAvailableHWEncoder(AV_CODEC_ID_H264);
  HWDecoderType decoder = DetectRecommendedHWDecoder();

  std::cout << "For optimal performance, use:\n" << std::endl;

  std::cout << "Encoder Configuration:" << std::endl;
  std::cout << "  EncoderConfig config;" << std::endl;
  if (encoder != HWEncoderType::kNone) {
    std::cout << "  config.encoder_type = EncoderType::kHardware;" << std::endl;
    std::cout << "  config.hw_encoder_type = HWEncoderType::"
              << (encoder == HWEncoderType::kNVENC ? "kNVENC"
                  : encoder == HWEncoderType::kQSV ? "kQSV"
                  : encoder == HWEncoderType::kAMF ? "kAMF"
                                                   : "kNone")
              << ";" << std::endl;
  } else {
    std::cout << "  config.encoder_type = EncoderType::kSoftware;" << std::endl;
  }

  std::cout << "\nDecoder Configuration:" << std::endl;
  std::cout << "  DecoderConfig config;" << std::endl;
  if (decoder != HWDecoderType::kNone) {
    std::cout << "  config.use_hw_decoder = true;" << std::endl;
    std::cout << "  config.hw_decoder_type = HWDecoderType::"
              << (decoder == HWDecoderType::kD3D11VA ? "kD3D11VA"
                  : decoder == HWDecoderType::kDXVA2 ? "kDXVA2"
                  : decoder == HWDecoderType::kCUDA  ? "kCUDA"
                                                     : "kNone")
              << ";" << std::endl;
  } else {
    std::cout << "  config.use_hw_decoder = false;" << std::endl;
  }

  std::cout << "\nRenderer Configuration:" << std::endl;
  if (decoder == HWDecoderType::kD3D11VA) {
    std::cout << "  RendererConfig config;" << std::endl;
    std::cout << "  config.renderer_type = RendererType::kD3D11;" << std::endl;
    std::cout << "  config.hw_context = &hw_decoder_context;  // For zero-copy"
              << std::endl;
  } else {
    std::cout << "  RendererConfig config;" << std::endl;
    std::cout << "  config.renderer_type = RendererType::kSDL;" << std::endl;
  }
}

int main(int argc, char* argv[]) {
  std::cout << "\n";
  std::cout
      << "╔════════════════════════════════════════════════════════════╗\n";
  std::cout
      << "║     ZenRemote Hardware Codec Detection Tool v1.0          ║\n";
  std::cout
      << "╚════════════════════════════════════════════════════════════╝\n";

  // 检查命令行参数
  bool verbose = false;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--verbose" || std::string(argv[i]) == "-v") {
      verbose = true;
    }
  }

  // 基础检测
  CheckHardwareEncoders();
  CheckHardwareDecoders();
  CheckHWAccelMethods();
  PerformanceEstimate();
  RecommendConfiguration();

  // 详细信息
  if (verbose) {
    ListAvailableEncoders();
    ListAvailableDecoders();
    CheckFFmpegConfiguration();
  } else {
    std::cout
        << "\n\033[90mRun with --verbose for detailed FFmpeg information\033[0m"
        << std::endl;
  }

  std::cout << "\n" << std::string(60, '=') << "\n" << std::endl;

  return 0;
}
