#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <iostream>

#include "../src/media/capture/screen_capturer.h"

using namespace zenremote::media::capture;

class ScreenCapturerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 设置日志
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("test", console_sink);
    logger->set_level(spdlog::level::debug);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
  }

  void TearDown() override { spdlog::drop_all(); }
};

/**
 * 测试 1: 初始化检查
 * 验证采集器是否能正确初始化
 */
TEST_F(ScreenCapturerTest, InitializeTest) {
  auto capturer = CreateScreenCapturer();
  ASSERT_NE(capturer, nullptr);

  CaptureConfig config;
  config.output_index = 0;
  config.target_fps = 30;
  config.enable_dirty_rect = true;
  config.enable_move_rect = true;

  bool result = capturer->Initialize(config);
  EXPECT_TRUE(result) << "Failed to initialize screen capturer";

  EXPECT_TRUE(capturer->IsInitialized()) << "Capturer should be initialized";

  // 检查分辨率
  int32_t width = 0, height = 0;
  capturer->GetResolution(width, height);
  EXPECT_GT(width, 0) << "Width should be positive";
  EXPECT_GT(height, 0) << "Height should be positive";

  std::cout << "Screen resolution: " << width << "x" << height << std::endl;

  // 检查像素格式
  EXPECT_EQ(capturer->GetPixelFormat(), PixelFormat::BGRA32);
}

/**
 * 测试 2: 采集帧
 * 验证是否能采集屏幕帧
 */
TEST_F(ScreenCapturerTest, CaptureFrameTest) {
  auto capturer = CreateScreenCapturer();
  CaptureConfig config;
  config.output_index = 0;
  ASSERT_TRUE(capturer->Initialize(config));
  ASSERT_TRUE(capturer->Start());

  // 采集 10 帧
  int frame_count = 0;
  for (int i = 0; i < 100; ++i) {
    auto frame = capturer->CaptureFrame();
    if (frame) {
      frame_count++;

      // 验证帧数据
      EXPECT_GT(frame->width, 0);
      EXPECT_GT(frame->height, 0);
      EXPECT_GT(frame->stride, 0);
      EXPECT_NE(frame->data, nullptr);
      EXPECT_GT(frame->size, 0);

      std::cout << "Frame " << frame_count << ": " << frame->width << "x"
                << frame->height
                << ", dirty_ratio=" << frame->metadata.dirty_ratio
                << ", fps=" << capturer->GetCurrentFps() << std::endl;

      capturer->ReleaseFrame();

      if (frame_count >= 10) {
        break;
      }
    }
  }

  EXPECT_GE(frame_count, 1) << "Should capture at least 1 frame";
  capturer->Stop();
}

/**
 * 测试 3: FPS 计算
 * 验证 FPS 计算是否正确
 */
TEST_F(ScreenCapturerTest, FpsCalculationTest) {
  auto capturer = CreateScreenCapturer();
  CaptureConfig config;
  config.output_index = 0;
  config.target_fps = 30;
  ASSERT_TRUE(capturer->Initialize(config));
  ASSERT_TRUE(capturer->Start());

  // 采集 60 帧,检查 FPS
  int captured = 0;
  auto start_time = std::chrono::steady_clock::now();

  for (int i = 0; i < 1000; ++i) {
    auto frame = capturer->CaptureFrame();
    if (frame) {
      captured++;
      capturer->ReleaseFrame();

      if (captured >= 60) {
        break;
      }
    }
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_time);

  uint32_t fps = capturer->GetCurrentFps();
  double actual_fps = (captured * 1000.0) / elapsed.count();

  std::cout << "Captured " << captured << " frames in " << elapsed.count()
            << "ms, actual FPS=" << actual_fps << ", reported FPS=" << fps
            << std::endl;

  EXPECT_GT(fps, 0) << "FPS should be positive";
  capturer->Stop();
}

/**
 * 测试 4: 脏区域检测
 * 验证脏区域元数据是否正确
 */
TEST_F(ScreenCapturerTest, DirtyRectTest) {
  auto capturer = CreateScreenCapturer();
  CaptureConfig config;
  config.output_index = 0;
  config.enable_dirty_rect = true;
  ASSERT_TRUE(capturer->Initialize(config));
  ASSERT_TRUE(capturer->Start());

  int frames_with_dirty = 0;
  for (int i = 0; i < 100; ++i) {
    auto frame = capturer->CaptureFrame();
    if (frame) {
      if (!frame->metadata.dirty_rects.empty()) {
        frames_with_dirty++;

        for (const auto& rect : frame->metadata.dirty_rects) {
          int32_t width = rect.right - rect.left;
          int32_t height = rect.bottom - rect.top;
          EXPECT_GT(width, 0);
          EXPECT_GT(height, 0);
        }

        if (frames_with_dirty == 1) {
          std::cout << "First frame with dirty rects: "
                    << frame->metadata.dirty_rects.size() << " rects"
                    << std::endl;
        }
      }

      capturer->ReleaseFrame();

      if (frames_with_dirty >= 3) {
        break;
      }
    }
  }

  EXPECT_GT(frames_with_dirty, 0) << "Should have frames with dirty rects";
  capturer->Stop();
}

/**
 * 测试 5: 关键帧强制
 * 验证强制关键帧是否工作
 */
TEST_F(ScreenCapturerTest, ForceKeyFrameTest) {
  auto capturer = CreateScreenCapturer();
  CaptureConfig config;
  config.output_index = 0;
  ASSERT_TRUE(capturer->Initialize(config));
  ASSERT_TRUE(capturer->Start());

  capturer->ForceKeyFrame();

  bool found_key_frame = false;
  for (int i = 0; i < 100; ++i) {
    auto frame = capturer->CaptureFrame();
    if (frame && frame->metadata.is_key_frame) {
      found_key_frame = true;
      std::cout << "Found forced key frame" << std::endl;
      capturer->ReleaseFrame();
      break;
    }
    if (frame) {
      capturer->ReleaseFrame();
    }
  }

  EXPECT_TRUE(found_key_frame) << "Should find forced key frame";
  capturer->Stop();
}

/**
 * 测试 6: 持续采集(压力测试)
 * 验证长时间采集的稳定性
 */
TEST_F(ScreenCapturerTest, ContinuousCaptureTest) {
  auto capturer = CreateScreenCapturer();
  CaptureConfig config;
  config.output_index = 0;
  ASSERT_TRUE(capturer->Initialize(config));
  ASSERT_TRUE(capturer->Start());

  int32_t width = 0, height = 0;
  capturer->GetResolution(width, height);

  // 采集 300 帧(10 秒 @ 30fps)
  int frame_count = 0;
  int error_count = 0;

  for (int i = 0; i < 10000; ++i) {
    auto frame = capturer->CaptureFrame();
    if (frame) {
      frame_count++;

      // 验证帧一致性
      if (frame->width != width || frame->height != height) {
        error_count++;
      }

      capturer->ReleaseFrame();

      if (frame_count >= 300) {
        break;
      }
    }
  }

  std::cout << "Captured " << frame_count << " frames, errors=" << error_count
            << ", FPS=" << capturer->GetCurrentFps() << std::endl;

  EXPECT_GT(frame_count, 100) << "Should capture many frames";
  EXPECT_EQ(error_count, 0) << "No frame inconsistencies expected";
  capturer->Stop();
}
