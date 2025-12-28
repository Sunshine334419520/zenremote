#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace zenremote {

inline uint32_t GetTimestampMs() {
  const auto now = std::chrono::steady_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch());
  return static_cast<uint32_t>(duration.count() & 0xFFFFFFFFU);
}

constexpr uint32_t kProtocolVersion = 1;

enum class ControlMessageType : uint8_t {
  kHandshake = 0x01,
  kHandshakeAck = 0x02,
  kInputEvent = 0x10,
  kInputAck = 0x11,
  kHeartbeat = 0x20,
};

struct ControlMessage {
  ControlMessageType type = ControlMessageType::kHandshake;
  uint16_t sequence = 0;
  uint32_t timestamp_ms = 0;
  std::vector<uint8_t> payload;
};

struct HandshakePayload {
  uint32_t version = kProtocolVersion;
  uint32_t session_id = 0;
  uint32_t ssrc = 0;
  uint8_t supported_codecs = 0;
  uint16_t capabilities_flags = 0;
};

enum class InputEventType : uint8_t {
  kMouseMove = 0,
  kMouseClick = 1,
  kMouseWheel = 2,
  kKeyDown = 3,
  kKeyUp = 4,
  kTouchEvent = 5,
};

struct InputEvent {
  InputEventType type = InputEventType::kMouseMove;
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t button = 0;
  uint8_t state = 0;
  int16_t wheel_delta = 0;
  uint32_t key_code = 0;
  uint32_t modifier_keys = 0;
};

struct AckPayload {
  uint16_t acked_sequence = 0;
  uint32_t original_timestamp_ms = 0;
};

inline void WriteUint16LE(uint16_t value, std::vector<uint8_t>& out) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

inline void WriteUint32LE(uint32_t value, std::vector<uint8_t>& out) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
}

inline uint16_t ReadUint16LE(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

inline uint32_t ReadUint32LE(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

inline std::vector<uint8_t> SerializeControlMessage(
    const ControlMessage& message) {
  std::vector<uint8_t> buffer;
  buffer.reserve(7 + message.payload.size());
  buffer.push_back(static_cast<uint8_t>(message.type));
  WriteUint16LE(message.sequence, buffer);
  WriteUint32LE(message.timestamp_ms, buffer);
  buffer.insert(buffer.end(), message.payload.begin(), message.payload.end());
  return buffer;
}

inline std::optional<ControlMessage> ParseControlMessage(const uint8_t* data,
                                                         size_t length) {
  if (!data || length < 7) {
    return std::nullopt;
  }

  ControlMessage message;
  message.type = static_cast<ControlMessageType>(data[0]);
  message.sequence = ReadUint16LE(data + 1);
  message.timestamp_ms = ReadUint32LE(data + 3);

  if (length > 7) {
    message.payload.assign(data + 7, data + length);
  }
  return message;
}

inline std::vector<uint8_t> SerializeHandshake(
    const HandshakePayload& handshake) {
  std::vector<uint8_t> payload;
  payload.reserve(14);
  WriteUint32LE(handshake.version, payload);
  WriteUint32LE(handshake.session_id, payload);
  WriteUint32LE(handshake.ssrc, payload);
  payload.push_back(handshake.supported_codecs);
  WriteUint16LE(handshake.capabilities_flags, payload);
  return payload;
}

inline std::optional<HandshakePayload> ParseHandshake(const uint8_t* data,
                                                      size_t length) {
  if (!data || length < 14) {
    return std::nullopt;
  }
  HandshakePayload payload;
  payload.version = ReadUint32LE(data);
  payload.session_id = ReadUint32LE(data + 4);
  payload.ssrc = ReadUint32LE(data + 8);
  payload.supported_codecs = data[12];
  payload.capabilities_flags = ReadUint16LE(data + 13);
  return payload;
}

inline std::vector<uint8_t> SerializeInputEvent(const InputEvent& event) {
  std::vector<uint8_t> payload;
  payload.reserve(17);  // 1+2+2+1+1+2+4+4 = 17 bytes
  payload.push_back(static_cast<uint8_t>(event.type));
  WriteUint16LE(event.x, payload);
  WriteUint16LE(event.y, payload);
  payload.push_back(event.button);
  payload.push_back(event.state);
  WriteUint16LE(static_cast<uint16_t>(event.wheel_delta), payload);
  WriteUint32LE(event.key_code, payload);
  WriteUint32LE(event.modifier_keys, payload);
  return payload;
}

inline std::optional<InputEvent> ParseInputEvent(const uint8_t* data,
                                                 size_t length) {
  if (!data || length < 17) {  // 1+2+2+1+1+2+4+4 = 17 bytes
    return std::nullopt;
  }
  InputEvent event;
  event.type = static_cast<InputEventType>(data[0]);
  event.x = ReadUint16LE(data + 1);
  event.y = ReadUint16LE(data + 3);
  event.button = data[5];
  event.state = data[6];
  event.wheel_delta = static_cast<int16_t>(ReadUint16LE(data + 7));
  event.key_code = ReadUint32LE(data + 9);
  event.modifier_keys = ReadUint32LE(data + 13);
  return event;
}

inline std::vector<uint8_t> SerializeAckPayload(const AckPayload& ack) {
  std::vector<uint8_t> payload;
  payload.reserve(6);
  WriteUint16LE(ack.acked_sequence, payload);
  WriteUint32LE(ack.original_timestamp_ms, payload);
  return payload;
}

inline std::optional<AckPayload> ParseAckPayload(const uint8_t* data,
                                                 size_t length) {
  if (!data || length < 6) {
    return std::nullopt;
  }
  AckPayload ack;
  ack.acked_sequence = ReadUint16LE(data);
  ack.original_timestamp_ms = ReadUint32LE(data + 2);
  return ack;
}

}  // namespace zenremote
