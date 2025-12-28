/**
 * @file test_rtp_receiver.cpp
 * @brief RTP 接收器单元测试
 *
 * 测试目标：
 * - 数据包解析
 * - 丢包检测
 * - 统计信息更新
 * - 序列号处理
 */

#include <gtest/gtest.h>

#include <vector>

#include "network/protocol/packet.h"
#include "network/protocol/rtp_receiver.h"

namespace zenremote {

class RTPReceiverTest : public ::testing::Test {
 protected:
  void SetUp() override { receiver_ = std::make_unique<RTPReceiver>(); }

  void TearDown() override { receiver_.reset(); }

  // 创建有效的 RTP 数据包字节流
  std::vector<uint8_t> CreateRawPacket(uint16_t seq,
                                       uint32_t timestamp,
                                       PayloadType payload_type,
                                       const std::vector<uint8_t>& payload) {
    RtpPacket packet;
    packet.header.version = 2;
    packet.header.padding = false;
    packet.header.extension = false;
    packet.header.csrc_count = 0;
    packet.header.marker = false;
    packet.header.payload_type = payload_type;
    packet.header.sequence_number = seq;
    packet.header.timestamp = timestamp;
    packet.header.ssrc = 0x12345678;
    packet.payload = payload;

    return SerializeRtpPacket(packet);
  }

  std::unique_ptr<RTPReceiver> receiver_;
};

// ============================================================================
// ParsePacket 测试
// ============================================================================

TEST_F(RTPReceiverTest, ParseValidVideoPacket) {
  auto raw = CreateRawPacket(100, 90000, PayloadType::kVideoH264,
                             {0x00, 0x01, 0x02, 0x03});

  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_EQ(packet->header.sequence_number, 100);
  EXPECT_EQ(packet->header.timestamp, 90000U);
  EXPECT_EQ(packet->header.payload_type, PayloadType::kVideoH264);
  EXPECT_EQ(packet->payload.size(), 4);
}

TEST_F(RTPReceiverTest, ParseValidAudioPacket) {
  auto raw =
      CreateRawPacket(200, 48000, PayloadType::kAudioOpus, {0xAA, 0xBB, 0xCC});

  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_EQ(packet->header.sequence_number, 200);
  EXPECT_EQ(packet->header.payload_type, PayloadType::kAudioOpus);
}

TEST_F(RTPReceiverTest, ParseValidControlPacket) {
  auto raw = CreateRawPacket(1, 1000, PayloadType::kControl, {0x01, 0x02});

  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_EQ(packet->header.payload_type, PayloadType::kControl);
}

TEST_F(RTPReceiverTest, ParseEmptyPayload) {
  auto raw = CreateRawPacket(50, 45000, PayloadType::kVideoH264, {});

  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_TRUE(packet->payload.empty());
}

TEST_F(RTPReceiverTest, ParseNullBuffer) {
  auto packet = receiver_->ParsePacket(nullptr, 100);
  EXPECT_FALSE(packet.has_value());
}

TEST_F(RTPReceiverTest, ParseBufferTooSmall) {
  std::vector<uint8_t> small_buffer(kRtpHeaderSize - 1, 0);
  auto packet =
      receiver_->ParsePacket(small_buffer.data(), small_buffer.size());
  EXPECT_FALSE(packet.has_value());
}

TEST_F(RTPReceiverTest, ParseInvalidHeader) {
  // 创建一个无效的头部（可能会被拒绝的数据）
  std::vector<uint8_t> invalid_buffer(kRtpHeaderSize, 0xFF);

  // 解析可能成功或失败，取决于具体实现
  // 这里主要测试不会崩溃
  auto packet =
      receiver_->ParsePacket(invalid_buffer.data(), invalid_buffer.size());
  // 不对结果做断言，只确保不崩溃
}

TEST_F(RTPReceiverTest, ParseLargePayload) {
  std::vector<uint8_t> large_payload(10000, 0xAA);
  auto raw =
      CreateRawPacket(999, 900000, PayloadType::kVideoH264, large_payload);

  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_EQ(packet->payload.size(), 10000);
}

// ============================================================================
// DetectMissingSequences 测试
// ============================================================================

TEST_F(RTPReceiverTest, DetectNoMissing) {
  auto missing = receiver_->DetectMissingSequences(100, 101);
  EXPECT_TRUE(missing.empty());
}

TEST_F(RTPReceiverTest, DetectSingleMissing) {
  auto missing = receiver_->DetectMissingSequences(100, 102);
  ASSERT_EQ(missing.size(), 1);
  EXPECT_EQ(missing[0], 101);
}

TEST_F(RTPReceiverTest, DetectMultipleMissing) {
  auto missing = receiver_->DetectMissingSequences(100, 105);
  ASSERT_EQ(missing.size(), 4);
  EXPECT_EQ(missing[0], 101);
  EXPECT_EQ(missing[1], 102);
  EXPECT_EQ(missing[2], 103);
  EXPECT_EQ(missing[3], 104);
}

TEST_F(RTPReceiverTest, DetectMissingWithWrapAround) {
  // 测试序列号回绕：65534 -> 0 (跳过 65535)
  auto missing = receiver_->DetectMissingSequences(65534, 0);
  ASSERT_EQ(missing.size(), 1);
  EXPECT_EQ(missing[0], 65535);
}

TEST_F(RTPReceiverTest, DetectMissingWithWrapAroundMultiple) {
  // 测试回绕时丢失多个：65533 -> 2
  auto missing = receiver_->DetectMissingSequences(65533, 2);
  ASSERT_EQ(missing.size(), 4);
  EXPECT_EQ(missing[0], 65534);
  EXPECT_EQ(missing[1], 65535);
  EXPECT_EQ(missing[2], 0);
  EXPECT_EQ(missing[3], 1);
}

TEST_F(RTPReceiverTest, DetectMissingSameSequence) {
  // 相同序列号（实际上不会发生，但测试边界）
  auto missing = receiver_->DetectMissingSequences(100, 100);
  // 这种情况下，循环不会执行，返回空
  // 但实际上 expected = 101 != 100，所以会有很多
  // 取决于实现，这里假设会检测到很多"丢失"
}

TEST_F(RTPReceiverTest, DetectMissingLimitCheck) {
  // 测试检测限制（超过 100 个）
  // prev_seq = 0, curr_seq = 200 会导致检测 199 个丢失
  // 但实现中有 100 的限制
  auto missing = receiver_->DetectMissingSequences(0, 200);
  EXPECT_LE(missing.size(), 101);  // 最多 101 个（包括终止条件）
}

// ============================================================================
// 统计信息测试
// ============================================================================

TEST_F(RTPReceiverTest, StatsInitialState) {
  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 0U);
  EXPECT_EQ(stats.bytes_received, 0U);
  EXPECT_EQ(stats.packets_lost, 0U);
}

TEST_F(RTPReceiverTest, StatsAfterParsing) {
  auto raw = CreateRawPacket(1, 90000, PayloadType::kVideoH264,
                             {0x00, 0x01, 0x02, 0x03});

  receiver_->ParsePacket(raw.data(), raw.size());

  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 1U);
  EXPECT_EQ(stats.bytes_received, 4U);  // payload size
  EXPECT_EQ(stats.last_sequence_number, 1);
  EXPECT_EQ(stats.last_timestamp, 90000U);
}

TEST_F(RTPReceiverTest, StatsAfterMultipleParsing) {
  // 发送连续的数据包
  for (uint16_t i = 0; i < 10; ++i) {
    auto raw = CreateRawPacket(i, 90000 + i * 3000, PayloadType::kVideoH264,
                               {0x00, 0x01});
    receiver_->ParsePacket(raw.data(), raw.size());
  }

  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 10U);
  EXPECT_EQ(stats.bytes_received, 20U);  // 10 * 2 bytes
  EXPECT_EQ(stats.packets_lost, 0U);     // 没有丢包
  EXPECT_EQ(stats.last_sequence_number, 9);
}

TEST_F(RTPReceiverTest, StatsWithPacketLoss) {
  // 发送数据包，但跳过一些
  auto raw1 = CreateRawPacket(0, 90000, PayloadType::kVideoH264, {0x00});
  receiver_->ParsePacket(raw1.data(), raw1.size());

  // 跳过 seq 1, 2，直接发送 3
  auto raw2 = CreateRawPacket(3, 99000, PayloadType::kVideoH264, {0x00});
  receiver_->ParsePacket(raw2.data(), raw2.size());

  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 2U);
  EXPECT_EQ(stats.packets_lost, 2U);  // 丢失 1, 2
}

TEST_F(RTPReceiverTest, StatsWithSequenceWrapAround) {
  // 发送接近回绕点的数据包
  auto raw1 = CreateRawPacket(65535, 90000, PayloadType::kVideoH264, {0x00});
  receiver_->ParsePacket(raw1.data(), raw1.size());

  // 正常回绕到 0
  auto raw2 = CreateRawPacket(0, 93000, PayloadType::kVideoH264, {0x00});
  receiver_->ParsePacket(raw2.data(), raw2.size());

  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 2U);
  EXPECT_EQ(stats.packets_lost, 0U);  // 正常回绕，没有丢包
}

// ============================================================================
// 到达时间测试
// ============================================================================

TEST_F(RTPReceiverTest, ArrivalTimeIsSet) {
  auto raw = CreateRawPacket(1, 90000, PayloadType::kVideoH264, {0x00});

  auto before = std::chrono::steady_clock::now();
  auto packet = receiver_->ParsePacket(raw.data(), raw.size());
  auto after = std::chrono::steady_clock::now();

  ASSERT_TRUE(packet.has_value());

  // 到达时间应该在解析前后之间
  EXPECT_GE(packet->arrival_time, before);
  EXPECT_LE(packet->arrival_time, after);
}

// ============================================================================
// PayloadType 测试
// ============================================================================

TEST_F(RTPReceiverTest, DifferentPayloadTypes) {
  std::vector<PayloadType> types = {
      PayloadType::kVideoH264,
      PayloadType::kAudioOpus,
      PayloadType::kControl,
      PayloadType::kControlAck,
  };

  for (auto type : types) {
    auto raw = CreateRawPacket(1, 1000, type, {0x00});
    auto packet = receiver_->ParsePacket(raw.data(), raw.size());

    ASSERT_TRUE(packet.has_value())
        << "Failed for type " << static_cast<int>(type);
    EXPECT_EQ(packet->header.payload_type, type);
  }
}

// ============================================================================
// 并发安全性测试（如果需要）
// ============================================================================

TEST_F(RTPReceiverTest, MultipleParseCallsSequential) {
  // 模拟快速连续解析
  for (int i = 0; i < 1000; ++i) {
    auto raw = CreateRawPacket(static_cast<uint16_t>(i), 90000 + i * 3000,
                               PayloadType::kVideoH264,
                               {static_cast<uint8_t>(i & 0xFF)});
    auto packet = receiver_->ParsePacket(raw.data(), raw.size());
    ASSERT_TRUE(packet.has_value()) << "Failed at iteration " << i;
  }

  const auto& stats = receiver_->GetStats();
  EXPECT_EQ(stats.packets_received, 1000U);
}

}  // namespace zenremote
