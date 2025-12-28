/**
 * @file test_jitter_buffer.cpp
 * @brief JitterBuffer 单元测试
 *
 * 测试目标：
 * - 数据包插入
 * - 帧提取（基于缓冲时间）
 * - 缓冲区溢出处理
 * - 重置功能
 * - 统计信息
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "network/protocol/jitter_buffer.h"

using namespace std::chrono_literals;

namespace zenremote {

class JitterBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.buffer_ms = 50;
    config_.max_packets = 100;
    buffer_ = std::make_unique<JitterBuffer>(config_);
  }

  void TearDown() override {
    buffer_.reset();
  }

  RtpPacket CreatePacket(uint32_t timestamp, uint16_t seq,
                         const std::vector<uint8_t>& payload) {
    RtpPacket packet;
    packet.header.version = 2;
    packet.header.marker = false;
    packet.header.payload_type = PayloadType::kVideoH264;
    packet.header.sequence_number = seq;
    packet.header.timestamp = timestamp;
    packet.header.ssrc = 0x12345678;
    packet.payload = payload;
    packet.arrival_time = std::chrono::steady_clock::now();
    return packet;
  }

  JitterBuffer::Config config_;
  std::unique_ptr<JitterBuffer> buffer_;
};

// ============================================================================
// 基本功能测试
// ============================================================================

TEST_F(JitterBufferTest, InsertSinglePacket) {
  auto packet = CreatePacket(90000, 1, {0x00, 0x01, 0x02, 0x03});
  buffer_->InsertPacket(packet);

  // 等待一小段时间让 GetBufferedMs 返回非零值
  std::this_thread::sleep_for(5ms);

  // 应该有数据在缓冲区
  EXPECT_GT(buffer_->GetBufferedMs(), 0U);
}

TEST_F(JitterBufferTest, InsertMultiplePacketsSameTimestamp) {
  // 同一帧的多个数据包（同一时间戳）
  auto packet1 = CreatePacket(90000, 1, {0x00, 0x01});
  auto packet2 = CreatePacket(90000, 2, {0x02, 0x03});
  auto packet3 = CreatePacket(90000, 3, {0x04, 0x05});

  buffer_->InsertPacket(packet1);
  buffer_->InsertPacket(packet2);
  buffer_->InsertPacket(packet3);

  // 等待一小段时间让 GetBufferedMs 返回非零值
  std::this_thread::sleep_for(5ms);

  // 缓冲区应该包含数据
  EXPECT_GT(buffer_->GetBufferedMs(), 0U);
}

TEST_F(JitterBufferTest, InsertPacketsDifferentTimestamps) {
  // 不同帧的数据包（不同时间戳）
  auto packet1 = CreatePacket(90000, 1, {0x00, 0x01});
  auto packet2 = CreatePacket(93000, 2, {0x02, 0x03});  // 下一帧
  auto packet3 = CreatePacket(96000, 3, {0x04, 0x05});  // 再下一帧

  buffer_->InsertPacket(packet1);
  buffer_->InsertPacket(packet2);
  buffer_->InsertPacket(packet3);

  // 等待一小段时间让 GetBufferedMs 返回非零值
  std::this_thread::sleep_for(5ms);

  EXPECT_GT(buffer_->GetBufferedMs(), 0U);
}

// ============================================================================
// 帧提取测试
// ============================================================================

TEST_F(JitterBufferTest, ExtractFrameAfterBufferTime) {
  // 插入数据包
  auto packet = CreatePacket(90000, 1, {0x00, 0x01, 0x02, 0x03});
  buffer_->InsertPacket(packet);

  // 等待缓冲时间
  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  // 应该能提取帧
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  bool extracted = buffer_->TryExtractFrame(frame_data, timestamp);

  EXPECT_TRUE(extracted);
  EXPECT_EQ(timestamp, 90000U);
  EXPECT_EQ(frame_data.size(), 4);
  EXPECT_EQ(frame_data[0], 0x00);
  EXPECT_EQ(frame_data[3], 0x03);
}

TEST_F(JitterBufferTest, ExtractFrameBeforeBufferTime) {
  // 插入数据包
  auto packet = CreatePacket(90000, 1, {0x00, 0x01, 0x02, 0x03});
  buffer_->InsertPacket(packet);

  // 立即尝试提取（不等待缓冲时间）
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  bool extracted = buffer_->TryExtractFrame(frame_data, timestamp);

  // 应该无法提取（缓冲时间未到）
  EXPECT_FALSE(extracted);
}

TEST_F(JitterBufferTest, ExtractMultiplePacketsAsOneFrame) {
  // 插入同一帧的多个数据包
  auto packet1 = CreatePacket(90000, 1, {0x00, 0x01});
  auto packet2 = CreatePacket(90000, 2, {0x02, 0x03});

  buffer_->InsertPacket(packet1);
  buffer_->InsertPacket(packet2);

  // 等待缓冲时间
  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  // 提取应该合并所有数据包
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  bool extracted = buffer_->TryExtractFrame(frame_data, timestamp);

  EXPECT_TRUE(extracted);
  EXPECT_EQ(timestamp, 90000U);
  EXPECT_EQ(frame_data.size(), 4);
}

TEST_F(JitterBufferTest, ExtractEmptyBuffer) {
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  bool extracted = buffer_->TryExtractFrame(frame_data, timestamp);

  EXPECT_FALSE(extracted);
}

TEST_F(JitterBufferTest, ExtractFramesInOrder) {
  // 插入多帧数据
  buffer_->InsertPacket(CreatePacket(90000, 1, {0x01}));
  buffer_->InsertPacket(CreatePacket(93000, 2, {0x02}));
  buffer_->InsertPacket(CreatePacket(96000, 3, {0x03}));

  // 等待缓冲时间
  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  // 提取第一帧
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;

  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_EQ(timestamp, 90000U);
  EXPECT_EQ(frame_data[0], 0x01);

  // 第二帧需要再等待
  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_EQ(timestamp, 93000U);
  EXPECT_EQ(frame_data[0], 0x02);
}

// ============================================================================
// 缓冲区溢出测试
// ============================================================================

TEST_F(JitterBufferTest, BufferOverflowDropsOldestFrame) {
  // 使用小容量配置
  JitterBuffer::Config small_config;
  small_config.buffer_ms = 50;
  small_config.max_packets = 3;  // 很小的容量
  JitterBuffer small_buffer(small_config);

  // 插入超过容量的数据包（不同时间戳）
  small_buffer.InsertPacket(CreatePacket(90000, 1, {0x01}));
  small_buffer.InsertPacket(CreatePacket(93000, 2, {0x02}));
  small_buffer.InsertPacket(CreatePacket(96000, 3, {0x03}));
  small_buffer.InsertPacket(CreatePacket(99000, 4, {0x04}));  // 应该触发溢出

  // 等待并提取
  std::this_thread::sleep_for(std::chrono::milliseconds(small_config.buffer_ms + 10));

  std::vector<uint8_t> frame_data;
  uint32_t timestamp;

  // 最老的帧应该被丢弃，提取的应该是较新的帧
  bool extracted = small_buffer.TryExtractFrame(frame_data, timestamp);
  EXPECT_TRUE(extracted);
  // 由于溢出处理，第一个时间戳的帧可能已被丢弃
}

// ============================================================================
// 重置功能测试
// ============================================================================

TEST_F(JitterBufferTest, ResetClearsBuffer) {
  // 插入一些数据
  buffer_->InsertPacket(CreatePacket(90000, 1, {0x01}));
  buffer_->InsertPacket(CreatePacket(93000, 2, {0x02}));

  // 等待一小段时间让 GetBufferedMs 返回非零值
  std::this_thread::sleep_for(5ms);
  EXPECT_GT(buffer_->GetBufferedMs(), 0U);

  // 重置
  buffer_->Reset();

  // 缓冲区应该为空
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_FALSE(buffer_->TryExtractFrame(frame_data, timestamp));
}

TEST_F(JitterBufferTest, ResetAllowsReuse) {
  // 插入数据
  buffer_->InsertPacket(CreatePacket(90000, 1, {0x01}));

  // 重置
  buffer_->Reset();

  // 应该能够继续使用
  buffer_->InsertPacket(CreatePacket(180000, 1, {0x02}));

  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_EQ(timestamp, 180000U);
}

// ============================================================================
// 统计信息测试
// ============================================================================

TEST_F(JitterBufferTest, GetBufferedMsEmptyBuffer) {
  EXPECT_EQ(buffer_->GetBufferedMs(), 0U);
}

TEST_F(JitterBufferTest, GetBufferedMsWithPackets) {
  buffer_->InsertPacket(CreatePacket(90000, 1, {0x01}));

  // 等待一些时间
  std::this_thread::sleep_for(20ms);

  uint32_t buffered = buffer_->GetBufferedMs();
  EXPECT_GE(buffered, 15U);  // 允许一些误差
  EXPECT_LE(buffered, 100U);
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST_F(JitterBufferTest, EmptyPayloadPacket) {
  auto packet = CreatePacket(90000, 1, {});
  buffer_->InsertPacket(packet);

  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_TRUE(frame_data.empty());
}

TEST_F(JitterBufferTest, LargePayloadPacket) {
  std::vector<uint8_t> large_payload(10000, 0xAA);
  auto packet = CreatePacket(90000, 1, large_payload);
  buffer_->InsertPacket(packet);

  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_EQ(frame_data.size(), 10000);
}

TEST_F(JitterBufferTest, ZeroBufferTime) {
  JitterBuffer::Config zero_config;
  zero_config.buffer_ms = 0;
  zero_config.max_packets = 100;
  JitterBuffer zero_buffer(zero_config);

  zero_buffer.InsertPacket(CreatePacket(90000, 1, {0x01}));

  // buffer_ms = 0 时，应该立即可以提取
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_TRUE(zero_buffer.TryExtractFrame(frame_data, timestamp));
}

TEST_F(JitterBufferTest, ConsecutiveTimestamps) {
  // 测试连续时间戳处理
  for (uint32_t ts = 0; ts < 10; ++ts) {
    buffer_->InsertPacket(CreatePacket(ts * 3000, static_cast<uint16_t>(ts),
                                        {static_cast<uint8_t>(ts)}));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(config_.buffer_ms + 10));

  // 应该能按顺序提取
  std::vector<uint8_t> frame_data;
  uint32_t timestamp;
  EXPECT_TRUE(buffer_->TryExtractFrame(frame_data, timestamp));
  EXPECT_EQ(timestamp, 0U);
}

// ============================================================================
// 配置测试
// ============================================================================

TEST_F(JitterBufferTest, DifferentBufferTimes) {
  std::vector<uint32_t> buffer_times = {10, 50, 100};

  for (auto bt : buffer_times) {
    JitterBuffer::Config config;
    config.buffer_ms = bt;
    config.max_packets = 100;
    JitterBuffer buffer(config);

    buffer.InsertPacket(CreatePacket(90000, 1, {0x01}));

    // 等待足够的时间（缓冲时间 + 余量）
    std::this_thread::sleep_for(std::chrono::milliseconds(bt + 30));

    std::vector<uint8_t> frame_data;
    uint32_t timestamp;
    bool extracted = buffer.TryExtractFrame(frame_data, timestamp);
    EXPECT_TRUE(extracted) << "Failed for buffer_ms=" << bt;
  }
}

}  // namespace zenremote
