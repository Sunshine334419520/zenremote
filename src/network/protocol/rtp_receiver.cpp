#include "network/protocol/rtp_receiver.h"

#include "common/log_manager.h"

namespace zenremote {

RTPReceiver::RTPReceiver() = default;

std::optional<ReceivedPacket> RTPReceiver::ReceivePacket(
    BaseConnection* connection,
    int timeout_ms) {
  if (!connection || !connection->IsOpen()) {
    ZENREMOTE_ERROR("Connection not open");
    return std::nullopt;
  }

  uint8_t buffer[65536];
  auto result = connection->Recv(buffer, sizeof(buffer), timeout_ms);

  if (result.IsErr()) {
    if (result.Code() != ErrorCode::kTimeout) {
      ZENREMOTE_ERROR("Failed to receive: {}", result.Message());
    }
    return std::nullopt;
  }

  size_t length = result.Value();
  return ParsePacket(buffer, length);
}

std::optional<ReceivedPacket> RTPReceiver::ParsePacket(const uint8_t* buffer,
                                                       size_t length) {
  auto rtp_packet = ParseRtpPacket(buffer, length);
  if (!rtp_packet.has_value()) {
    ZENREMOTE_WARN("Failed to parse RTP packet");
    return std::nullopt;
  }

  ReceivedPacket packet;
  packet.header = rtp_packet->header;
  packet.payload = rtp_packet->payload;
  packet.arrival_time = std::chrono::steady_clock::now();

  UpdateStats(packet);

  return packet;
}

std::vector<uint16_t> RTPReceiver::DetectMissingSequences(uint16_t prev_seq,
                                                          uint16_t curr_seq) {
  std::vector<uint16_t> missing;

  uint16_t expected = static_cast<uint16_t>(prev_seq + 1);
  while (expected != curr_seq) {
    missing.push_back(expected);
    expected = static_cast<uint16_t>(expected + 1);

    if (missing.size() > 100) {
      ZENREMOTE_ERROR("Too many missing sequences, possible reset");
      break;
    }
  }

  return missing;
}

void RTPReceiver::UpdateStats(const ReceivedPacket& packet) {
  stats_.packets_received++;
  stats_.bytes_received += packet.payload.size();
  stats_.last_sequence_number = packet.header.sequence_number;
  stats_.last_timestamp = packet.header.timestamp;

  if (has_received_first_packet_) {
    if (packet.header.sequence_number != expected_sequence_number_) {
      auto missing = DetectMissingSequences(
          static_cast<uint16_t>(expected_sequence_number_ - 1),
          packet.header.sequence_number);
      stats_.packets_lost += missing.size();

      if (!missing.empty()) {
        ZENREMOTE_WARN("Detected {} missing packet(s)", missing.size());
      }
    }
  } else {
    has_received_first_packet_ = true;
  }

  expected_sequence_number_ =
      static_cast<uint16_t>(packet.header.sequence_number + 1);
}

}  // namespace zenremote
