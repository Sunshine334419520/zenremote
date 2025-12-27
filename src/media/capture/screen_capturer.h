#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace zenremote::media::capture {

/**
 * 屏幕采集的通用数据结构
 */

// 像素格式
enum class PixelFormat {
  RGBA32,  // 32-bit RGBA (8888)
  BGRA32,  // 32-bit BGRA (8888)
};

// 脏区域(改变的区域)
struct DirtyRect {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;

  int32_t Width() const { return right - left; }
  int32_t Height() const { return bottom - top; }
};

// 移动区域(滚动/拖动导致的整块移动)
struct MoveRect {
  DirtyRect source;
  DirtyRect destination;
};

// 帧元数据
struct FrameMetadata {
  // 帧的时间戳(GPU 时钟,微秒)
  int64_t timestamp_us;

  // 脏区域(改变的像素区域)
  std::vector<DirtyRect> dirty_rects;

  // 移动区域(整块区域移动,如窗口拖动)
  std::vector<MoveRect> move_rects;

  // 是否是关键帧(需要编码整个屏幕)
  // true: 初始帧 或 DWM 重置 或 用户强制
  bool is_key_frame;

  // 累积跳过的帧数(CPU 处理能力不足)
  // 0: 正常
  // > 0: 说明 CPU 跟不上采集速率,已有帧被丢弃
  uint32_t accumulated_frames;

  // 脏区域所占屏幕比例(0.0 - 1.0)
  // 用于编码器判断是否进行区域编码优化
  float dirty_ratio;
};

// 采集的帧数据
struct Frame {
  // 帧宽度和高度(像素)
  int32_t width;
  int32_t height;

  // 步长(一行占用的字节数,可能大于 width * 4)
  int32_t stride;

  // 像素格式
  PixelFormat format;

  // 帧数据指针(GPU 显存地址,仅在采集周期内有效)
  // 重要: 调用 ReleaseFrame() 前必须复制或处理,否则指针失效
  const uint8_t* data;

  // 帧大小(字节数)
  size_t size;

  // 帧元数据
  FrameMetadata metadata;
};

// 屏幕采集器配置
struct CaptureConfig {
  // 采集的显示器索引(0 = 主显示器)
  uint32_t output_index = 0;

  // 目标帧率(fps)
  // 实际帧率可能更低(取决于屏幕变化和 CPU 性能)
  uint32_t target_fps = 30;

  // 是否启用脏区域优化
  // true: 只采集改变的区域(带宽节省 30-70%)
  // false: 始终采集完整屏幕(简单,但浪费带宽)
  bool enable_dirty_rect = true;

  // 是否启用移动区域优化
  // true: 检测整块区域移动,优化编码
  // false: 所有变化都作为脏区域处理
  bool enable_move_rect = true;
};

/**
 * 抽象的屏幕采集器接口
 *
 * 使用示例:
 *   auto capturer = CreateScreenCapturer(CaptureConfig{});
 *   capturer->Start();
 *   while (running) {
 *       auto frame = capturer->CaptureFrame();
 *       if (frame) {
 *           encoder->Encode(frame.value());
 *           capturer->ReleaseFrame();  // 重要!释放帧
 *       }
 *   }
 *   capturer->Stop();
 */
class ScreenCapturer {
 public:
  virtual ~ScreenCapturer() = default;

  /**
   * 初始化采集器
   * @return true if successful
   */
  virtual bool Initialize(const CaptureConfig& config) = 0;

  /**
   * 启动采集(创建采集线程)
   * @return true if successful
   */
  virtual bool Start() = 0;

  /**
   * 停止采集(销毁采集线程)
   */
  virtual void Stop() = 0;

  /**
   * 采集一帧
   *
   * 调用此函数后:
   *   1. 如果返回有效的 Frame,数据指针在调用 ReleaseFrame() 前有效
   *   2. 数据指针指向 GPU 显存,不可长期持有
   *   3. 必须尽快处理或复制,然后调用 ReleaseFrame()
   *
   * @return 有效的 Frame,或 std::nullopt(无新帧)
   */
  virtual std::optional<Frame> CaptureFrame() = 0;

  /**
   * 释放当前帧
   *
   * 重要: 在使用完 CaptureFrame() 返回的数据后,必须调用此函数
   * 否则 DXGI 会堆积缓冲区,导致延迟增加
   */
  virtual void ReleaseFrame() = 0;

  /**
   * 强制生成关键帧(下一帧标记为 is_key_frame = true)
   * 用于错误恢复或编码器同步
   */
  virtual void ForceKeyFrame() = 0;

  /**
   * 获取采集的屏幕分辨率
   */
  virtual void GetResolution(int32_t& width, int32_t& height) const = 0;

  /**
   * 获取采集的像素格式
   */
  virtual PixelFormat GetPixelFormat() const = 0;

  /**
   * 获取当前的帧率(已采集的实际帧率)
   */
  virtual uint32_t GetCurrentFps() const = 0;

  /**
   * 检查是否初始化成功
   */
  virtual bool IsInitialized() const = 0;
};

/**
 * 工厂函数:创建平台相关的屏幕采集器
 *
 * Windows: 使用 DXGI Desktop Duplication
 * macOS: 使用 SCStreamCaptureKit 或 CGDisplayStream
 */
std::unique_ptr<ScreenCapturer> CreateScreenCapturer();

}  // namespace zenremote::media::capture
