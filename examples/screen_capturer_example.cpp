/**
 * 屏幕采集示例程序
 *
 * 演示如何使用 ScreenCapturer 进行屏幕采集,并保存为 BMP 文件
 *
 * 编译:
 *   �?Visual Studio 中编译此项目即可
 *
 * 运行:
 *   ScreenCapturerExample.exe
 *
 * 输出:
 *   - screenshot_*.bmp: 采集的屏幕截�?5 �?
 *   - capture_stats.txt: 采集统计信息
 */

#include <windows.h>

#include <chrono>
#include <fstream>
#include <iostream>

#include "src/common/log_manager.h"
#include "src/media/capture/screen_capturer.h"

using namespace zenremote::media::capture;

/**
 * �?BGRA32 帧保存为 BMP 文件
 */
bool SaveFrameAsBmp(const Frame& frame, const std::string& filename) {
  // BMP 文件头结�?
  struct BmpFileHeader {
    uint16_t signature;  // "BM"
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
  };

  struct BmpInfoHeader {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
  };

  BmpFileHeader file_header{};
  BmpInfoHeader info_header{};

  // 计算 BMP 信息
  int32_t width = frame.width;
  int32_t height = frame.height;

  // BMP 文件按行从下到上存储,行必须对齐到 4 字节边界
  int32_t row_size = ((width * 32 + 31) / 32) * 4;

  file_header.signature = 0x4D42;  // "BM"
  file_header.offset = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader);
  file_header.size = file_header.offset + (row_size * height);

  info_header.size = sizeof(BmpInfoHeader);
  info_header.width = width;
  info_header.height = height;
  info_header.planes = 1;
  info_header.bits_per_pixel = 32;
  info_header.compression = 0;  // 无压�?
  info_header.image_size = row_size * height;

  // 写入 BMP 文件
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    ZENREMOTE_ERROR("Failed to open file: {}", filename);
    return false;
  }

  // 写文件头
  file.write(reinterpret_cast<char*>(&file_header), sizeof(BmpFileHeader));
  file.write(reinterpret_cast<char*>(&info_header), sizeof(BmpInfoHeader));

  // 写像素数�?(从下到上,BMP 格式要求)
  // DXGI 返回的是从上到下的数�?所以需要反�?
  for (int32_t y = height - 1; y >= 0; --y) {
    const uint8_t* row_data = frame.data + (y * frame.stride);
    file.write(reinterpret_cast<const char*>(row_data), frame.stride);

    // 对齐�?4 字节边界
    int32_t padding = row_size - frame.stride;
    if (padding > 0) {
      std::vector<uint8_t> pad(padding, 0);
      file.write(reinterpret_cast<char*>(pad.data()), padding);
    }
  }

  file.close();
  ZENREMOTE_INFO("Saved screenshot to: {}", filename);
  return true;
}

struct CaptureStats {
  int total_frames = 0;
  int key_frames = 0;
  double total_dirty_ratio = 0;
  uint32_t min_fps = UINT32_MAX;
  uint32_t max_fps = 0;
  int64_t total_latency_us = 0;
};

int main() {
  // ================== 日志配置 ==================
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("ScreenCapture", console_sink);
  logger->set_level(spdlog::level::info);
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);

  ZENREMOTE_INFO("========== Screen Capturer Example ==========");
  ZENREMOTE_INFO("This program demonstrates DXGI screen capture");
  ZENREMOTE_INFO("");

  // ================== 创建采集�?==================
  auto capturer = CreateScreenCapturer();
  if (!capturer) {
    ZENREMOTE_ERROR("Failed to create screen capturer");
    return -1;
  }

  // ================== 配置采集参数 ==================
  CaptureConfig config;
  config.output_index = 0;  // 主显示器
  config.target_fps = 30;
  config.enable_dirty_rect = true;
  config.enable_move_rect = true;

  ZENREMOTE_INFO("Config:");
  ZENREMOTE_INFO("  Output index: {}", config.output_index);
  ZENREMOTE_INFO("  Target FPS: {}", config.target_fps);
  ZENREMOTE_INFO("  Dirty rect enabled: {}", config.enable_dirty_rect);
  ZENREMOTE_INFO("  Move rect enabled: {}", config.enable_move_rect);

  // ================== 初始化采集器 ==================
  if (!capturer->Initialize(config)) {
    ZENREMOTE_ERROR("Failed to initialize screen capturer");
    return -1;
  }

  int32_t width = 0, height = 0;
  capturer->GetResolution(width, height);

  ZENREMOTE_INFO("");
  ZENREMOTE_INFO("Screen Info:");
  ZENREMOTE_INFO("  Resolution: {}x{}", width, height);
  ZENREMOTE_INFO(
      "  Pixel format: {}",
      (capturer->GetPixelFormat() == PixelFormat::BGRA32 ? "BGRA32"
                                                         : "RGBA32"));

  // ================== 启动采集 ==================
  if (!capturer->Start()) {
    ZENREMOTE_ERROR("Failed to start screen capturer");
    return -1;
  }

  ZENREMOTE_INFO("");
  ZENREMOTE_INFO("Capture started, please perform some screen activities");
  ZENREMOTE_INFO("(move mouse, type, open/close windows, etc.)");
  ZENREMOTE_INFO("");

  // ================== 采集帧并保存截图 ==================
  CaptureStats stats;
  int screenshots_taken = 0;
  auto capture_start_time = std::chrono::steady_clock::now();

  for (int i = 0; i < 10000 && screenshots_taken < 5; ++i) {
    auto frame = capturer->CaptureFrame();

    if (!frame) {
      // 没有新帧,继续等待
      continue;
    }

    auto frame_time = std::chrono::steady_clock::now();

    // ===== 收集统计信息 =====
    stats.total_frames++;
    if (frame->metadata.is_key_frame) {
      stats.key_frames++;
    }
    stats.total_dirty_ratio += frame->metadata.dirty_ratio;
    uint32_t current_fps = capturer->GetCurrentFps();
    stats.min_fps = std::min(stats.min_fps, current_fps);
    stats.max_fps = std::max(stats.max_fps, current_fps);

    // ===== �?20 帧保存一次截�?=====
    if (stats.total_frames % 20 == 0) {
      char filename[256];
      snprintf(filename, sizeof(filename), "screenshot_%d.bmp",
               screenshots_taken);

      if (SaveFrameAsBmp(*frame, filename)) {
        screenshots_taken++;
        ZENREMOTE_INFO("Saved screenshot {} (Frame {}, dirty_ratio={:.2f}%)",
                       screenshots_taken, stats.total_frames,
                       frame->metadata.dirty_ratio * 100);
      }
    }

    // ===== 打印帧信�?�?30 帧一�? =====
    if (stats.total_frames % 30 == 0) {
      ZENREMOTE_DEBUG(
          "Frame {}: "
          "dirty_rects={}, move_rects={}, dirty_ratio={:.2f}%, "
          "key_frame={}, accumulated_frames={}, fps={}",
          stats.total_frames, frame->metadata.dirty_rects.size(),
          frame->metadata.move_rects.size(), frame->metadata.dirty_ratio * 100,
          frame->metadata.is_key_frame ? "yes" : "no",
          frame->metadata.accumulated_frames, capturer->GetCurrentFps());
    }

    // ===== 释放�?必须!) =====
    capturer->ReleaseFrame();

    // 给屏幕采集一些处理时�?
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ================== 停止采集 ==================
  capturer->Stop();

  auto capture_end_time = std::chrono::steady_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      capture_end_time - capture_start_time);

  // ================== 输出统计信息 ==================
  ZENREMOTE_INFO("");
  ZENREMOTE_INFO("========== Capture Statistics ==========");
  ZENREMOTE_INFO("Total frames: {}", stats.total_frames);
  ZENREMOTE_INFO(
      "Key frames: {} ({:.1f}%)", stats.key_frames,
      (stats.total_frames > 0
           ? (static_cast<double>(stats.key_frames) / stats.total_frames * 100)
           : 0));
  ZENREMOTE_INFO("Average dirty ratio: {:.1f}%",
                 stats.total_frames > 0
                     ? (stats.total_dirty_ratio / stats.total_frames * 100)
                     : 0);
  ZENREMOTE_INFO("FPS range: {} - {} (reported)", stats.min_fps, stats.max_fps);
  ZENREMOTE_INFO(
      "Actual FPS: {:.1f}",
      static_cast<double>(stats.total_frames * 1000) / total_duration.count());
  ZENREMOTE_INFO("Total duration: {} ms", total_duration.count());
  ZENREMOTE_INFO("Screenshots saved: {}", screenshots_taken);

  // ================== 保存统计信息到文�?==================
  std::ofstream stats_file("capture_stats.txt");
  stats_file << "Screen Capture Statistics\n";
  stats_file << "========================\n\n";
  stats_file << "Screen Resolution: " << width << "x" << height << "\n";
  stats_file << "Total Frames: " << stats.total_frames << "\n";
  stats_file << "Key Frames: " << stats.key_frames << " ("
             << (stats.total_frames > 0
                     ? (static_cast<double>(stats.key_frames) /
                        stats.total_frames * 100)
                     : 0)
             << "%)\n";
  stats_file << "Average Dirty Ratio: "
             << (stats.total_frames > 0
                     ? (stats.total_dirty_ratio / stats.total_frames * 100)
                     : 0)
             << "%\n";
  stats_file << "FPS Range: " << stats.min_fps << " - " << stats.max_fps
             << "\n";
  stats_file << "Actual FPS: "
             << (static_cast<double>(stats.total_frames * 1000) /
                 total_duration.count())
             << "\n";
  stats_file << "Total Duration: " << total_duration.count() << " ms\n";
  stats_file << "Screenshots Saved: " << screenshots_taken << "\n";
  stats_file.close();

  ZENREMOTE_INFO("");
  ZENREMOTE_INFO("Statistics saved to: capture_stats.txt");
  ZENREMOTE_INFO("");
  ZENREMOTE_INFO("Example completed successfully!");
  ZENREMOTE_INFO("Check the screenshot_*.bmp and capture_stats.txt files");

  return 0;
}
