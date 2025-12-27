/**
 * 改进后的屏幕采集示例 - 验证 API 可行�?
 *
 * 演示�?
 * 1. 外部控制帧率（示例使�?30fps�?
 * 2. 处理帧数�?
 * 3. 统计采集性能
 */

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#include "../src/common/log_manager.h"
#include "../src/media/capture/screen_capturer.h"

using namespace zenremote::media::capture;
using Clock = std::chrono::steady_clock;

int main() {
  // 初始化日�?
  try {
    auto logger =
        spdlog::basic_logger_mt("screen_capture", "capture_example.log");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::err);
  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "Log init failed: " << ex.what() << std::endl;
  }

  ZENREMOTE_INFO("=== 屏幕采集示例 ===");

  // 初始化采集器
  CaptureConfig config;
  config.output_index = 0;
  config.target_fps = 30;  // 配置（内部不做帧率限制）
  config.enable_dirty_rect = true;
  config.enable_move_rect = true;

  auto capturer = CreateScreenCapturer();
  if (!capturer->Initialize(config)) {
    ZENREMOTE_ERROR("Failed to initialize screen capturer");
    return 1;
  }

  int32_t width, height;
  capturer->GetResolution(width, height);
  ZENREMOTE_INFO("Resolution: {}x{}", width, height);

  capturer->Start();

  // 外部实现帧率控制
  const uint32_t TARGET_FPS = 30;
  const uint32_t FRAME_INTERVAL_MS = 1000 / TARGET_FPS;  // 33ms
  const int TOTAL_FRAMES = 300;

  int frame_count = 0;
  uint32_t total_key_frames = 0;
  float avg_dirty_ratio = 0;

  auto start_time = Clock::now();
  auto last_frame_time = start_time;

  while (frame_count < TOTAL_FRAMES) {
    // 外部帧率控制
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_frame_time);

    auto sleep_ms = FRAME_INTERVAL_MS - elapsed.count();
    if (sleep_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    // 采集�?
    if (auto frame = capturer->CaptureFrame()) {
      frame_count++;

      if (frame->metadata.is_key_frame) {
        total_key_frames++;
      }
      avg_dirty_ratio += frame->metadata.dirty_ratio;

      capturer->ReleaseFrame();
      last_frame_time = Clock::now();

      if (frame_count % 30 == 0) {
        uint32_t current_fps = capturer->GetCurrentFps();
        ZENREMOTE_INFO("Frame {}: FPS={}", frame_count, current_fps);
      }
    }
  }

  auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now() - start_time);

  capturer->Stop();
  capturer->Shutdown();

  // 输出统计
  ZENREMOTE_INFO("=== 采集完成 ===");
  ZENREMOTE_INFO("Total frames: {}", frame_count);
  ZENREMOTE_INFO("Key frames: {}", total_key_frames);
  ZENREMOTE_INFO("Avg dirty ratio: {:.1f}%",
                 (avg_dirty_ratio / frame_count) * 100);
  ZENREMOTE_INFO("Elapsed: {}ms", total_elapsed.count());

  float actual_fps = frame_count * 1000.0f / total_elapsed.count();
  ZENREMOTE_INFO("Actual FPS: {:.1f}", actual_fps);

  std::cout << "\n采集完成，查�?capture_example.log 获取详细信息\n";

  return 0;
}
