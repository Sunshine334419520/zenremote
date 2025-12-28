/**
 * @file test_pacer.cpp
 * @brief Pacer（发送节奏控制器）单元测试
 *
 * 测试目标：
 * - 发送速率控制
 * - 批量发送限制
 * - 时间间隔控制
 * - 重置功能
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "network/protocol/pacer.h"

using namespace std::chrono_literals;

namespace zenremote {

class PacerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.pacing_interval_ms = 5;
    config_.max_packets_per_batch = 10;
    pacer_ = std::make_unique<Pacer>(config_);
  }

  void TearDown() override { pacer_.reset(); }

  Pacer::Config config_;
  std::unique_ptr<Pacer> pacer_;
};

// ============================================================================
// 基本功能测试
// ============================================================================

TEST_F(PacerTest, CanSendInitially) {
  // 刚创建的 Pacer 应该允许发送
  EXPECT_TRUE(pacer_->CanSend());
}

TEST_F(PacerTest, CanSendMultiplePacketsInBatch) {
  // 在一个批次内应该能发送多个数据包
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    EXPECT_TRUE(pacer_->CanSend()) << "Failed at packet " << i;
    pacer_->OnPacketSent();
  }
}

TEST_F(PacerTest, CannotExceedBatchLimit) {
  // 发送到批次上限
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    EXPECT_TRUE(pacer_->CanSend());
    pacer_->OnPacketSent();
  }

  // 超过批次上限后不能立即发送
  EXPECT_FALSE(pacer_->CanSend());
}

TEST_F(PacerTest, CanSendAfterInterval) {
  // 发送到批次上限
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    pacer_->OnPacketSent();
  }

  EXPECT_FALSE(pacer_->CanSend());

  // 等待间隔时间后应该可以发送
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.pacing_interval_ms + 2));

  EXPECT_TRUE(pacer_->CanSend());
}

// ============================================================================
// OnPacketSent 测试
// ============================================================================

TEST_F(PacerTest, OnPacketSentIncrementsBatchCount) {
  // 初始状态可以发送
  EXPECT_TRUE(pacer_->CanSend());

  // 发送一个数据包
  pacer_->OnPacketSent();

  // 仍然可以发送（在批次限制内）
  EXPECT_TRUE(pacer_->CanSend());
}

TEST_F(PacerTest, OnPacketSentResetsBatchAfterInterval) {
  // 发送到批次上限
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    pacer_->OnPacketSent();
  }

  EXPECT_FALSE(pacer_->CanSend());

  // 等待间隔
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.pacing_interval_ms + 2));

  // 发送一个新数据包应该重置批次计数
  pacer_->OnPacketSent();

  // 应该还能继续发送
  EXPECT_TRUE(pacer_->CanSend());
}

// ============================================================================
// Reset 测试
// ============================================================================

TEST_F(PacerTest, ResetAllowsSending) {
  // 发送到批次上限
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    pacer_->OnPacketSent();
  }

  EXPECT_FALSE(pacer_->CanSend());

  // 重置
  pacer_->Reset();

  // 应该可以发送
  EXPECT_TRUE(pacer_->CanSend());
}

TEST_F(PacerTest, ResetClearsBatchCount) {
  // 发送一些数据包
  pacer_->OnPacketSent();
  pacer_->OnPacketSent();
  pacer_->OnPacketSent();

  // 重置
  pacer_->Reset();

  // 应该能发送完整批次
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    EXPECT_TRUE(pacer_->CanSend()) << "Failed at packet " << i;
    pacer_->OnPacketSent();
  }
}

// ============================================================================
// 不同配置测试
// ============================================================================

TEST_F(PacerTest, SmallBatchSize) {
  Pacer::Config small_config;
  small_config.pacing_interval_ms = 10;
  small_config.max_packets_per_batch = 2;
  Pacer small_pacer(small_config);

  // 发送两个数据包
  EXPECT_TRUE(small_pacer.CanSend());
  small_pacer.OnPacketSent();

  EXPECT_TRUE(small_pacer.CanSend());
  small_pacer.OnPacketSent();

  // 第三个应该被阻止
  EXPECT_FALSE(small_pacer.CanSend());
}

TEST_F(PacerTest, LargeBatchSize) {
  Pacer::Config large_config;
  large_config.pacing_interval_ms = 5;
  large_config.max_packets_per_batch = 100;
  Pacer large_pacer(large_config);

  // 应该能发送 100 个数据包
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(large_pacer.CanSend()) << "Failed at packet " << i;
    large_pacer.OnPacketSent();
  }

  // 第 101 个应该被阻止
  EXPECT_FALSE(large_pacer.CanSend());
}

TEST_F(PacerTest, ShortInterval) {
  Pacer::Config short_config;
  short_config.pacing_interval_ms = 1;
  short_config.max_packets_per_batch = 5;
  Pacer short_pacer(short_config);

  // 发送到批次上限
  for (int i = 0; i < 5; ++i) {
    short_pacer.OnPacketSent();
  }

  EXPECT_FALSE(short_pacer.CanSend());

  // 短暂等待后应该可以发送
  std::this_thread::sleep_for(3ms);

  EXPECT_TRUE(short_pacer.CanSend());
}

TEST_F(PacerTest, LongInterval) {
  Pacer::Config long_config;
  long_config.pacing_interval_ms = 100;
  long_config.max_packets_per_batch = 5;
  Pacer long_pacer(long_config);

  // 发送到批次上限
  for (int i = 0; i < 5; ++i) {
    long_pacer.OnPacketSent();
  }

  EXPECT_FALSE(long_pacer.CanSend());

  // 短暂等待（小于间隔）后仍然不能发送
  std::this_thread::sleep_for(50ms);

  EXPECT_FALSE(long_pacer.CanSend());

  // 等待完整间隔后可以发送
  std::this_thread::sleep_for(60ms);

  EXPECT_TRUE(long_pacer.CanSend());
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST_F(PacerTest, SinglePacketBatch) {
  Pacer::Config single_config;
  single_config.pacing_interval_ms = 5;
  single_config.max_packets_per_batch = 1;
  Pacer single_pacer(single_config);

  EXPECT_TRUE(single_pacer.CanSend());
  single_pacer.OnPacketSent();

  // 发送一个后就不能再发
  EXPECT_FALSE(single_pacer.CanSend());

  // 等待间隔后可以发送
  std::this_thread::sleep_for(7ms);
  EXPECT_TRUE(single_pacer.CanSend());
}

TEST_F(PacerTest, ZeroInterval) {
  // 测试零间隔（虽然实际使用中不太合理）
  Pacer::Config zero_config;
  zero_config.pacing_interval_ms = 0;
  zero_config.max_packets_per_batch = 5;
  Pacer zero_pacer(zero_config);

  // 发送到批次上限
  for (int i = 0; i < 5; ++i) {
    zero_pacer.OnPacketSent();
  }

  // 由于间隔为 0，应该总是可以发送（间隔检查会立即通过）
  EXPECT_TRUE(zero_pacer.CanSend());
}

// ============================================================================
// 连续发送模拟测试
// ============================================================================

TEST_F(PacerTest, ContinuousSending) {
  int packets_sent = 0;
  auto start = std::chrono::steady_clock::now();

  // 模拟 50ms 内的连续发送
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
             .count() < 50) {
    if (pacer_->CanSend()) {
      pacer_->OnPacketSent();
      packets_sent++;
    } else {
      std::this_thread::sleep_for(1ms);
    }
  }

  // 在 50ms 内应该能发送相当数量的数据包
  // 基于配置：5ms 间隔，每批 10 个
  // 放宽要求，因为系统调度可能有延迟
  EXPECT_GT(packets_sent, 20);   // 至少 20 个
  EXPECT_LE(packets_sent, 200);  // 不超过 200 个
}

TEST_F(PacerTest, BurstThenPause) {
  // 突发发送
  int sent_in_burst = 0;
  while (pacer_->CanSend()) {
    pacer_->OnPacketSent();
    sent_in_burst++;
  }

  EXPECT_EQ(sent_in_burst, static_cast<int>(config_.max_packets_per_batch));

  // 暂停后继续
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.pacing_interval_ms + 2));

  int sent_after_pause = 0;
  while (pacer_->CanSend()) {
    pacer_->OnPacketSent();
    sent_after_pause++;
  }

  EXPECT_EQ(sent_after_pause, static_cast<int>(config_.max_packets_per_batch));
}

// ============================================================================
// 时间精度测试
// ============================================================================

TEST_F(PacerTest, TimingAccuracy) {
  // 这个测试验证时间控制的基本准确性
  auto start = std::chrono::steady_clock::now();

  // 发送一批
  for (int i = 0; i < static_cast<int>(config_.max_packets_per_batch); ++i) {
    pacer_->OnPacketSent();
  }

  EXPECT_FALSE(pacer_->CanSend());

  // 等待刚好不够的时间
  std::this_thread::sleep_for(
      std::chrono::milliseconds(config_.pacing_interval_ms - 2));

  // 可能仍然不能发送（取决于调度精度）
  // 这里不做断言，因为时间精度在不同系统上差异很大

  // 再等待一些时间
  std::this_thread::sleep_for(5ms);

  // 现在应该可以发送
  EXPECT_TRUE(pacer_->CanSend());
}

}  // namespace zenremote
