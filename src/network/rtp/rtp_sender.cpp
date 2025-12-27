#include "rtp_sender.h"

#include <cstring>
#include <vector>

#include "network/connection/base_connection.h"
#include "network/rtp/rtp_packet.h"

namespace zenremote {

RTPSender::RTPSender(BaseConnection* connection, const Config& config)
    : connection_(connection), config_(config) {}

RTPSender::~RTPSender() = default;

Result<void> RTPSender::SendVideoFrame(const uint8_t* data,
                                       size_t length,
                                       uint32_t timestamp,
                                       bool is_last_packet) {
  if (!connection_) {
    return Result<void>::Err(ErrorCode::kNetworkError, "No connection");
  }

  network::RTPHeader header;
  header.version = 2;
  header.padding = 0;
  header.extension = 0;
  header.csrc_count = 0;
  header.marker = is_last_packet ? 1 : 0;
  header.payload_type = config_.payload_type;
  header.sequence_number = sequence_number_++;
  header.timestamp = timestamp;
  header.ssrc = config_.ssrc;

  std::vector<uint8_t> packet(sizeof(network::RTPHeader) + length);
  std::memcpy(packet.data(), &header, sizeof(network::RTPHeader));
  std::memcpy(packet.data() + sizeof(network::RTPHeader), data, length);

  auto result = connection_->Send(packet.data(), packet.size());
  if (result.IsErr()) {
    return Result<void>::Err(result.Code(), result.Message());
  }

  stats_.packets_sent++;
  stats_.bytes_sent += packet.size();

  return Result<void>::Ok();
}

Result<void> RTPSender::SendAudioPacket(const uint8_t* data,
                                        size_t length,
                                        uint32_t timestamp) {
  return SendVideoFrame(data, length, timestamp, true);
}

}  // namespace zenremote
