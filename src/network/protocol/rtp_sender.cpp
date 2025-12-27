#include "network/protocol/rtp_sender.h"

#include "common/log_manager.h"

namespace zenremote {

RTPSender::RTPSender(uint32_t ssrc, BaseConnection* connection)
    : ssrc_(ssrc), connection_(connection) {}

bool RTPSender::SendVideoFrame(const uint8_t* data,
                               size_t length,
                               uint32_t timestamp_90khz,
                               bool marker) {
  if (!connection_ || !connection_->IsOpen()) {
    ZENREMOTE_ERROR("Connection not open");
    return false;
  }

  RtpPacket packet;
  packet.header = BuildHeader(PayloadType::kVideoH264, video_sequence_number_++,
                              timestamp_90khz, marker);
  packet.payload.assign(data, data + length);

  auto buffer = SerializeRtpPacket(packet);
  if (buffer.empty()) {
    ZENREMOTE_ERROR("Failed to serialize video RTP packet");
    return false;
  }

  auto result = connection_->Send(buffer.data(), buffer.size());
  if (result.IsOk()) {
    stats_.packets_sent++;
    stats_.bytes_sent += buffer.size();
    stats_.last_sequence_number = packet.header.sequence_number;
    return true;
  }

  ZENREMOTE_ERROR("Failed to send video packet: {}", result.Message());
  return false;
}

bool RTPSender::SendAudioPacket(const uint8_t* data,
                                size_t length,
                                uint32_t timestamp_48khz) {
  if (!connection_ || !connection_->IsOpen()) {
    ZENREMOTE_ERROR("Connection not open");
    return false;
  }

  RtpPacket packet;
  packet.header = BuildHeader(PayloadType::kAudioOpus, audio_sequence_number_++,
                              timestamp_48khz, false);
  packet.payload.assign(data, data + length);

  auto buffer = SerializeRtpPacket(packet);
  if (buffer.empty()) {
    ZENREMOTE_ERROR("Failed to serialize audio RTP packet");
    return false;
  }

  auto result = connection_->Send(buffer.data(), buffer.size());
  if (result.IsOk()) {
    stats_.packets_sent++;
    stats_.bytes_sent += buffer.size();
    stats_.last_sequence_number = packet.header.sequence_number;
    return true;
  }

  ZENREMOTE_ERROR("Failed to send audio packet: {}", result.Message());
  return false;
}

bool RTPSender::SendControlMessage(const uint8_t* data,
                                   size_t length,
                                   uint32_t timestamp_ms) {
  if (!connection_ || !connection_->IsOpen()) {
    ZENREMOTE_ERROR("Connection not open");
    return false;
  }

  RtpPacket packet;
  packet.header = BuildHeader(PayloadType::kControl, control_sequence_number_++,
                              timestamp_ms, false);
  packet.payload.assign(data, data + length);

  auto buffer = SerializeRtpPacket(packet);
  if (buffer.empty()) {
    ZENREMOTE_ERROR("Failed to serialize control RTP packet");
    return false;
  }

  auto result = connection_->Send(buffer.data(), buffer.size());
  if (result.IsOk()) {
    stats_.packets_sent++;
    stats_.bytes_sent += buffer.size();
    stats_.last_sequence_number = packet.header.sequence_number;
    return true;
  }

  ZENREMOTE_ERROR("Failed to send control packet: {}", result.Message());
  return false;
}

bool RTPSender::SendRawRtpPacket(const RtpPacket& packet) {
  if (!connection_ || !connection_->IsOpen()) {
    ZENREMOTE_ERROR("Connection not open");
    return false;
  }

  auto buffer = SerializeRtpPacket(packet);
  if (buffer.empty()) {
    ZENREMOTE_ERROR("Failed to serialize RTP packet");
    return false;
  }

  auto result = connection_->Send(buffer.data(), buffer.size());
  if (result.IsOk()) {
    stats_.packets_sent++;
    stats_.bytes_sent += buffer.size();
    stats_.last_sequence_number = packet.header.sequence_number;
    return true;
  }

  ZENREMOTE_ERROR("Failed to send RTP packet: {}", result.Message());
  return false;
}

RtpHeader RTPSender::BuildHeader(PayloadType payload_type,
                                 uint16_t seq,
                                 uint32_t timestamp,
                                 bool marker) {
  RtpHeader header;
  header.version = kRtpVersion;
  header.padding = false;
  header.extension = false;
  header.csrc_count = 0;
  header.marker = marker;
  header.payload_type = payload_type;
  header.sequence_number = seq;
  header.timestamp = timestamp;
  header.ssrc = ssrc_;
  return header;
}

}  // namespace zenremote
