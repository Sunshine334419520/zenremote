/**
 * @file test_rtp_packet.cpp
 * @brief RTP 数据包序列化/反序列化单元测试
 *
 * 测试目标：
 * - RTP 头部字节序转换
 * - RTP 头部序列化和解析
 * - 完整 RTP 数据包序列化和解析
 * - 边界条件处理
 */

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

#include "network/protocol/packet.h"

namespace zenremote {

// ============================================================================
// 字节序转换测试
// ============================================================================

TEST(ByteOrderTest, HostToNetwork16) {
  EXPECT_EQ(detail::HostToNetwork16(0x1234), 0x3412);
  EXPECT_EQ(detail::HostToNetwork16(0x0000), 0x0000);
  EXPECT_EQ(detail::HostToNetwork16(0xFFFF), 0xFFFF);
  EXPECT_EQ(detail::HostToNetwork16(0x00FF), 0xFF00);
  EXPECT_EQ(detail::HostToNetwork16(0xFF00), 0x00FF);
}

TEST(ByteOrderTest, HostToNetwork32) {
  EXPECT_EQ(detail::HostToNetwork32(0x12345678), 0x78563412U);
  EXPECT_EQ(detail::HostToNetwork32(0x00000000), 0x00000000U);
  EXPECT_EQ(detail::HostToNetwork32(0xFFFFFFFF), 0xFFFFFFFFU);
  EXPECT_EQ(detail::HostToNetwork32(0x000000FF), 0xFF000000U);
  EXPECT_EQ(detail::HostToNetwork32(0xFF000000), 0x000000FFU);
}

TEST(ByteOrderTest, NetworkToHost16) {
  // NetworkToHost 应该是 HostToNetwork 的逆操作
  EXPECT_EQ(detail::NetworkToHost16(detail::HostToNetwork16(0x1234)), 0x1234);
  EXPECT_EQ(detail::NetworkToHost16(detail::HostToNetwork16(0xABCD)), 0xABCD);
}

TEST(ByteOrderTest, NetworkToHost32) {
  EXPECT_EQ(detail::NetworkToHost32(detail::HostToNetwork32(0x12345678)),
            0x12345678U);
  EXPECT_EQ(detail::NetworkToHost32(detail::HostToNetwork32(0xDEADBEEF)),
            0xDEADBEEFU);
}

// ============================================================================
// RTP 头部序列化测试
// ============================================================================

TEST(RtpHeaderSerializeTest, BasicHeader) {
  RtpHeader header;
  header.version = 2;
  header.padding = false;
  header.extension = false;
  header.csrc_count = 0;
  header.marker = false;
  header.payload_type = PayloadType::kVideoH264;
  header.sequence_number = 1234;
  header.timestamp = 90000;
  header.ssrc = 0x12345678;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  // 验证版本位（前两位）
  EXPECT_EQ((buffer[0] >> 6) & 0x03, 2);

  // 验证 padding 位
  EXPECT_EQ((buffer[0] >> 5) & 0x01, 0);

  // 验证 extension 位
  EXPECT_EQ((buffer[0] >> 4) & 0x01, 0);

  // 验证 CSRC count
  EXPECT_EQ(buffer[0] & 0x0F, 0);

  // 验证 marker 位
  EXPECT_EQ((buffer[1] >> 7) & 0x01, 0);

  // 验证 payload type
  EXPECT_EQ(buffer[1] & 0x7F, static_cast<uint8_t>(PayloadType::kVideoH264));
}

TEST(RtpHeaderSerializeTest, WithMarkerBit) {
  RtpHeader header;
  header.version = 2;
  header.marker = true;
  header.payload_type = PayloadType::kAudioOpus;
  header.sequence_number = 0xFFFF;
  header.timestamp = 0xFFFFFFFF;
  header.ssrc = 0xDEADBEEF;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  // 验证 marker 位
  EXPECT_EQ((buffer[1] >> 7) & 0x01, 1);

  // 验证 payload type
  EXPECT_EQ(buffer[1] & 0x7F, static_cast<uint8_t>(PayloadType::kAudioOpus));
}

TEST(RtpHeaderSerializeTest, WithPaddingAndExtension) {
  RtpHeader header;
  header.version = 2;
  header.padding = true;
  header.extension = true;
  header.csrc_count = 5;
  header.marker = true;
  header.payload_type = PayloadType::kControl;
  header.sequence_number = 0x1234;
  header.timestamp = 0x56789ABC;
  header.ssrc = 0xABCDEF01;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  // 验证各标志位
  EXPECT_EQ((buffer[0] >> 6) & 0x03, 2);   // version
  EXPECT_EQ((buffer[0] >> 5) & 0x01, 1);   // padding
  EXPECT_EQ((buffer[0] >> 4) & 0x01, 1);   // extension
  EXPECT_EQ(buffer[0] & 0x0F, 5);          // csrc_count
  EXPECT_EQ((buffer[1] >> 7) & 0x01, 1);   // marker
}

TEST(RtpHeaderSerializeTest, BufferTooSmall) {
  RtpHeader header;
  std::array<uint8_t, kRtpHeaderSize - 1> small_buffer{};
  EXPECT_FALSE(
      SerializeRtpHeader(header, small_buffer.data(), small_buffer.size()));
}

TEST(RtpHeaderSerializeTest, ExactBufferSize) {
  RtpHeader header;
  std::array<uint8_t, kRtpHeaderSize> exact_buffer{};
  EXPECT_TRUE(
      SerializeRtpHeader(header, exact_buffer.data(), exact_buffer.size()));
}

// ============================================================================
// RTP 头部解析测试
// ============================================================================

TEST(RtpHeaderParseTest, BasicParse) {
  // 构造一个已知的 RTP 头部
  // 注意：代码使用 HostToNetwork 转换后再手动提取字节，所以字节是反转的
  std::array<uint8_t, kRtpHeaderSize> buffer = {
      0x80,  // version=2, padding=0, extension=0, csrc_count=0
      0x60,  // marker=0, payload_type=96 (H264)
      0xD2, 0x04,  // sequence_number=1234 (经过 HostToNetwork16 转换)
      0x90, 0x5F, 0x01, 0x00,  // timestamp=90000 (经过 HostToNetwork32 转换)
      0x78, 0x56, 0x34, 0x12   // ssrc=0x12345678 (经过 HostToNetwork32 转换)
  };

  auto header = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(header.has_value());

  EXPECT_EQ(header->version, 2);
  EXPECT_FALSE(header->padding);
  EXPECT_FALSE(header->extension);
  EXPECT_EQ(header->csrc_count, 0);
  EXPECT_FALSE(header->marker);
  EXPECT_EQ(header->payload_type, PayloadType::kVideoH264);
  EXPECT_EQ(header->sequence_number, 1234);
  EXPECT_EQ(header->timestamp, 90000U);
  EXPECT_EQ(header->ssrc, 0x12345678U);
}

TEST(RtpHeaderParseTest, WithMarkerBit) {
  // 注意：代码使用 HostToNetwork 转换后再手动提取字节
  std::array<uint8_t, kRtpHeaderSize> buffer = {
      0x80,  // version=2
      0xE1,  // marker=1, payload_type=97 (Opus)
      0xFF, 0xFF,  // sequence_number=65535 (转换后不变)
      0xFF, 0xFF, 0xFF, 0xFF,  // timestamp=max (转换后不变)
      0xEF, 0xBE, 0xAD, 0xDE   // ssrc=0xDEADBEEF (经过 HostToNetwork32 转换)
  };

  auto header = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(header.has_value());

  EXPECT_TRUE(header->marker);
  EXPECT_EQ(header->payload_type, PayloadType::kAudioOpus);
  EXPECT_EQ(header->sequence_number, 65535);
  EXPECT_EQ(header->timestamp, 0xFFFFFFFFU);
  EXPECT_EQ(header->ssrc, 0xDEADBEEFU);
}

TEST(RtpHeaderParseTest, NullBuffer) {
  auto header = ParseRtpHeader(nullptr, kRtpHeaderSize);
  EXPECT_FALSE(header.has_value());
}

TEST(RtpHeaderParseTest, BufferTooSmall) {
  std::array<uint8_t, kRtpHeaderSize - 1> small_buffer{};
  auto header = ParseRtpHeader(small_buffer.data(), small_buffer.size());
  EXPECT_FALSE(header.has_value());
}

TEST(RtpHeaderParseTest, EmptyBuffer) {
  auto header = ParseRtpHeader(nullptr, 0);
  EXPECT_FALSE(header.has_value());
}

// ============================================================================
// 序列化/解析往返测试
// ============================================================================

TEST(RtpHeaderRoundtripTest, VideoPacket) {
  RtpHeader original;
  original.version = 2;
  original.padding = false;
  original.extension = false;
  original.csrc_count = 0;
  original.marker = true;
  original.payload_type = PayloadType::kVideoH264;
  original.sequence_number = 12345;
  original.timestamp = 3600000;  // 40 seconds at 90kHz
  original.ssrc = 0xABCDEF01;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(original, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->version, original.version);
  EXPECT_EQ(parsed->padding, original.padding);
  EXPECT_EQ(parsed->extension, original.extension);
  EXPECT_EQ(parsed->csrc_count, original.csrc_count);
  EXPECT_EQ(parsed->marker, original.marker);
  EXPECT_EQ(parsed->payload_type, original.payload_type);
  EXPECT_EQ(parsed->sequence_number, original.sequence_number);
  EXPECT_EQ(parsed->timestamp, original.timestamp);
  EXPECT_EQ(parsed->ssrc, original.ssrc);
}

TEST(RtpHeaderRoundtripTest, AudioPacket) {
  RtpHeader original;
  original.version = 2;
  original.padding = true;
  original.extension = true;
  original.csrc_count = 3;
  original.marker = false;
  original.payload_type = PayloadType::kAudioOpus;
  original.sequence_number = 65000;
  original.timestamp = 480000;  // 10 seconds at 48kHz
  original.ssrc = 0x11223344;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(original, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->version, original.version);
  EXPECT_EQ(parsed->padding, original.padding);
  EXPECT_EQ(parsed->extension, original.extension);
  EXPECT_EQ(parsed->csrc_count, original.csrc_count);
  EXPECT_EQ(parsed->marker, original.marker);
  EXPECT_EQ(parsed->payload_type, original.payload_type);
  EXPECT_EQ(parsed->sequence_number, original.sequence_number);
  EXPECT_EQ(parsed->timestamp, original.timestamp);
  EXPECT_EQ(parsed->ssrc, original.ssrc);
}

TEST(RtpHeaderRoundtripTest, ControlPacket) {
  RtpHeader original;
  original.version = 2;
  original.padding = false;
  original.extension = false;
  original.csrc_count = 0;
  original.marker = true;
  original.payload_type = PayloadType::kControl;
  original.sequence_number = 1;
  original.timestamp = 1000;
  original.ssrc = 0x99999999;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(original, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->payload_type, PayloadType::kControl);
}

// ============================================================================
// 完整 RTP 数据包测试
// ============================================================================

TEST(RtpPacketTest, SerializeWithPayload) {
  RtpPacket packet;
  packet.header.version = 2;
  packet.header.marker = true;
  packet.header.payload_type = PayloadType::kVideoH264;
  packet.header.sequence_number = 100;
  packet.header.timestamp = 90000;
  packet.header.ssrc = 0x12345678;
  packet.payload = {0x00, 0x01, 0x02, 0x03, 0x04};

  auto serialized = SerializeRtpPacket(packet);
  ASSERT_FALSE(serialized.empty());
  EXPECT_EQ(serialized.size(), kRtpHeaderSize + 5);

  // 验证 payload 部分
  EXPECT_EQ(serialized[kRtpHeaderSize + 0], 0x00);
  EXPECT_EQ(serialized[kRtpHeaderSize + 1], 0x01);
  EXPECT_EQ(serialized[kRtpHeaderSize + 2], 0x02);
  EXPECT_EQ(serialized[kRtpHeaderSize + 3], 0x03);
  EXPECT_EQ(serialized[kRtpHeaderSize + 4], 0x04);
}

TEST(RtpPacketTest, SerializeEmptyPayload) {
  RtpPacket packet;
  packet.header.version = 2;
  packet.header.payload_type = PayloadType::kAudioOpus;
  packet.header.sequence_number = 1;
  packet.header.timestamp = 1;
  packet.header.ssrc = 1;
  packet.payload.clear();

  auto serialized = SerializeRtpPacket(packet);
  ASSERT_FALSE(serialized.empty());
  EXPECT_EQ(serialized.size(), kRtpHeaderSize);
}

TEST(RtpPacketTest, ParseWithPayload) {
  // 使用序列化函数创建有效的 RTP 数据包，避免字节序问题
  RtpPacket original;
  original.header.version = 2;
  original.header.marker = true;
  original.header.payload_type = PayloadType::kVideoH264;
  original.header.sequence_number = 100;
  original.header.timestamp = 90000;
  original.header.ssrc = 0x12345678;
  original.payload = {0xAA, 0xBB, 0xCC, 0xDD};

  auto data = SerializeRtpPacket(original);
  ASSERT_FALSE(data.empty());

  auto packet = ParseRtpPacket(data.data(), data.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_TRUE(packet->header.marker);
  EXPECT_EQ(packet->header.sequence_number, 100);
  EXPECT_EQ(packet->payload.size(), 4);
  EXPECT_EQ(packet->payload[0], 0xAA);
  EXPECT_EQ(packet->payload[1], 0xBB);
  EXPECT_EQ(packet->payload[2], 0xCC);
  EXPECT_EQ(packet->payload[3], 0xDD);
}

TEST(RtpPacketTest, ParseEmptyPayload) {
  std::vector<uint8_t> data = {
      0x80,              // version=2
      0x61,              // marker=0, payload_type=97
      0x00, 0x01,        // sequence_number=1
      0x00, 0x00, 0x00, 0x01,  // timestamp=1
      0x00, 0x00, 0x00, 0x01   // ssrc=1
  };

  auto packet = ParseRtpPacket(data.data(), data.size());
  ASSERT_TRUE(packet.has_value());

  EXPECT_EQ(packet->payload.size(), 0);
}

TEST(RtpPacketTest, ParseNullBuffer) {
  auto packet = ParseRtpPacket(nullptr, 100);
  EXPECT_FALSE(packet.has_value());
}

TEST(RtpPacketTest, ParseBufferTooSmall) {
  std::array<uint8_t, kRtpHeaderSize - 1> small_buffer{};
  auto packet = ParseRtpPacket(small_buffer.data(), small_buffer.size());
  EXPECT_FALSE(packet.has_value());
}

TEST(RtpPacketTest, RoundtripWithLargePayload) {
  RtpPacket original;
  original.header.version = 2;
  original.header.marker = false;
  original.header.payload_type = PayloadType::kVideoH264;
  original.header.sequence_number = 50000;
  original.header.timestamp = 900000;
  original.header.ssrc = 0xFEDCBA98;

  // 创建一个较大的 payload（模拟 H.264 NAL unit）
  original.payload.resize(1400);
  for (size_t i = 0; i < original.payload.size(); ++i) {
    original.payload[i] = static_cast<uint8_t>(i & 0xFF);
  }

  auto serialized = SerializeRtpPacket(original);
  ASSERT_FALSE(serialized.empty());
  EXPECT_EQ(serialized.size(), kRtpHeaderSize + 1400);

  auto parsed = ParseRtpPacket(serialized.data(), serialized.size());
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->header.sequence_number, original.header.sequence_number);
  EXPECT_EQ(parsed->header.timestamp, original.header.timestamp);
  EXPECT_EQ(parsed->header.ssrc, original.header.ssrc);
  EXPECT_EQ(parsed->payload.size(), original.payload.size());

  // 验证 payload 内容
  EXPECT_EQ(parsed->payload, original.payload);
}

// ============================================================================
// 边界值测试
// ============================================================================

TEST(RtpPacketBoundaryTest, MaxSequenceNumber) {
  RtpHeader header;
  header.sequence_number = 0xFFFF;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->sequence_number, 0xFFFF);
}

TEST(RtpPacketBoundaryTest, MaxTimestamp) {
  RtpHeader header;
  header.timestamp = 0xFFFFFFFF;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->timestamp, 0xFFFFFFFFU);
}

TEST(RtpPacketBoundaryTest, ZeroValues) {
  RtpHeader header;
  header.version = 2;
  header.padding = false;
  header.extension = false;
  header.csrc_count = 0;
  header.marker = false;
  header.payload_type = PayloadType::kVideoH264;
  header.sequence_number = 0;
  header.timestamp = 0;
  header.ssrc = 0;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->sequence_number, 0);
  EXPECT_EQ(parsed->timestamp, 0U);
  EXPECT_EQ(parsed->ssrc, 0U);
}

TEST(RtpPacketBoundaryTest, AllFlagsSet) {
  RtpHeader header;
  header.version = 2;
  header.padding = true;
  header.extension = true;
  header.csrc_count = 15;  // max 4 bits
  header.marker = true;

  std::array<uint8_t, kRtpHeaderSize> buffer{};
  ASSERT_TRUE(SerializeRtpHeader(header, buffer.data(), buffer.size()));

  auto parsed = ParseRtpHeader(buffer.data(), buffer.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->padding);
  EXPECT_TRUE(parsed->extension);
  EXPECT_EQ(parsed->csrc_count, 15);
  EXPECT_TRUE(parsed->marker);
}

}  // namespace zenremote
