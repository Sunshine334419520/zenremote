#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace zenremote {

enum class PayloadType : uint8_t {
  kVideoH264 = 96,
  kAudioOpus = 97,
  kControl = 98,
  kControlAck = 99,
};

constexpr uint8_t kRtpVersion = 2U;
constexpr size_t kRtpHeaderSize = 12U;

struct RtpHeader {
  uint8_t version = kRtpVersion;
  bool padding = false;
  bool extension = false;
  uint8_t csrc_count = 0;
  bool marker = false;
  PayloadType payload_type = PayloadType::kVideoH264;
  uint16_t sequence_number = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;
};

struct RtpPacket {
  RtpHeader header;
  std::vector<uint8_t> payload;
  std::chrono::steady_clock::time_point arrival_time;
};

namespace detail {
inline uint16_t HostToNetwork16(uint16_t value) {
  return static_cast<uint16_t>((value >> 8U) | (value << 8U));
}

inline uint32_t HostToNetwork32(uint32_t value) {
  return ((value & 0x000000FFU) << 24U) | ((value & 0x0000FF00U) << 8U) |
         ((value & 0x00FF0000U) >> 8U) | ((value & 0xFF000000U) >> 24U);
}

inline uint16_t NetworkToHost16(uint16_t value) {
  return HostToNetwork16(value);
}

inline uint32_t NetworkToHost32(uint32_t value) {
  return HostToNetwork32(value);
}
}  // namespace detail

inline bool SerializeRtpHeader(const RtpHeader& header,
                               uint8_t* buffer,
                               size_t buffer_size) {
  if (buffer_size < kRtpHeaderSize) {
    return false;
  }

  buffer[0] = static_cast<uint8_t>((header.version & 0x03U) << 6U);
  buffer[0] |= static_cast<uint8_t>((header.padding ? 1U : 0U) << 5U);
  buffer[0] |= static_cast<uint8_t>((header.extension ? 1U : 0U) << 4U);
  buffer[0] |= static_cast<uint8_t>(header.csrc_count & 0x0FU);

  buffer[1] = static_cast<uint8_t>((header.marker ? 1U : 0U) << 7U);
  buffer[1] |=
      static_cast<uint8_t>(static_cast<uint8_t>(header.payload_type) & 0x7FU);

  const uint16_t seq = detail::HostToNetwork16(header.sequence_number);
  buffer[2] = static_cast<uint8_t>((seq >> 8U) & 0xFFU);
  buffer[3] = static_cast<uint8_t>(seq & 0xFFU);

  const uint32_t timestamp = detail::HostToNetwork32(header.timestamp);
  buffer[4] = static_cast<uint8_t>((timestamp >> 24U) & 0xFFU);
  buffer[5] = static_cast<uint8_t>((timestamp >> 16U) & 0xFFU);
  buffer[6] = static_cast<uint8_t>((timestamp >> 8U) & 0xFFU);
  buffer[7] = static_cast<uint8_t>(timestamp & 0xFFU);

  const uint32_t ssrc = detail::HostToNetwork32(header.ssrc);
  buffer[8] = static_cast<uint8_t>((ssrc >> 24U) & 0xFFU);
  buffer[9] = static_cast<uint8_t>((ssrc >> 16U) & 0xFFU);
  buffer[10] = static_cast<uint8_t>((ssrc >> 8U) & 0xFFU);
  buffer[11] = static_cast<uint8_t>(ssrc & 0xFFU);

  return true;
}

inline std::optional<RtpHeader> ParseRtpHeader(const uint8_t* buffer,
                                               size_t length) {
  if (!buffer || length < kRtpHeaderSize) {
    return std::nullopt;
  }

  RtpHeader header;
  header.version = static_cast<uint8_t>((buffer[0] >> 6U) & 0x03U);
  header.padding = ((buffer[0] >> 5U) & 0x01U) != 0;
  header.extension = ((buffer[0] >> 4U) & 0x01U) != 0;
  header.csrc_count = static_cast<uint8_t>(buffer[0] & 0x0FU);
  header.marker = ((buffer[1] >> 7U) & 0x01U) != 0;
  header.payload_type = static_cast<PayloadType>(buffer[1] & 0x7FU);

  uint16_t seq = static_cast<uint16_t>((buffer[2] << 8U) | buffer[3]);
  header.sequence_number = detail::NetworkToHost16(seq);

  uint32_t timestamp = (static_cast<uint32_t>(buffer[4]) << 24U) |
                       (static_cast<uint32_t>(buffer[5]) << 16U) |
                       (static_cast<uint32_t>(buffer[6]) << 8U) |
                       static_cast<uint32_t>(buffer[7]);
  header.timestamp = detail::NetworkToHost32(timestamp);

  uint32_t ssrc = (static_cast<uint32_t>(buffer[8]) << 24U) |
                  (static_cast<uint32_t>(buffer[9]) << 16U) |
                  (static_cast<uint32_t>(buffer[10]) << 8U) |
                  static_cast<uint32_t>(buffer[11]);
  header.ssrc = detail::NetworkToHost32(ssrc);

  return header;
}

inline std::vector<uint8_t> SerializeRtpPacket(const RtpPacket& packet) {
  std::vector<uint8_t> buffer(kRtpHeaderSize + packet.payload.size());
  if (!SerializeRtpHeader(packet.header, buffer.data(), buffer.size())) {
    return {};
  }
  if (!packet.payload.empty()) {
    std::copy(packet.payload.begin(), packet.payload.end(),
              buffer.begin() + kRtpHeaderSize);
  }
  return buffer;
}

inline std::optional<RtpPacket> ParseRtpPacket(const uint8_t* buffer,
                                               size_t length) {
  if (!buffer || length < kRtpHeaderSize) {
    return std::nullopt;
  }

  auto header = ParseRtpHeader(buffer, length);
  if (!header.has_value()) {
    return std::nullopt;
  }

  RtpPacket packet;
  packet.header = header.value();
  packet.arrival_time = std::chrono::steady_clock::now();
  packet.payload.resize(length - kRtpHeaderSize);
  if (!packet.payload.empty()) {
    std::copy(buffer + kRtpHeaderSize, buffer + length, packet.payload.begin());
  }
  return packet;
}

}  // namespace zenremote
