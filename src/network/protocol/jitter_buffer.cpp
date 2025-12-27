#include "network/protocol/jitter_buffer.h"

#include "common/log_manager.h"

namespace zenremote {

JitterBuffer::JitterBuffer(const Config& config) : config_(config) {}

void JitterBuffer::InsertPacket(const RtpPacket& packet) {
  if (buffer_.size() >= config_.max_packets) {
    ZENREMOTE_WARN("JitterBuffer overflow, dropping oldest frame");
    buffer_.erase(buffer_.begin());
  }

  PacketInfo info;
  info.packet = packet;
  info.insert_time = std::chrono::steady_clock::now();

  if (!has_first_packet_) {
    first_packet_time_ = info.insert_time;
    has_first_packet_ = true;
  }

  buffer_[packet.header.timestamp].push_back(info);

  if (!has_expected_timestamp_) {
    expected_timestamp_ = packet.header.timestamp;
    has_expected_timestamp_ = true;
  }
}

bool JitterBuffer::TryExtractFrame(std::vector<uint8_t>& frame_data,
                                   uint32_t& timestamp) {
  if (buffer_.empty()) {
    return false;
  }

  auto now = std::chrono::steady_clock::now();

  auto it = buffer_.begin();
  if (!it->second.empty()) {
    auto buffered_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - it->second.front().insert_time)
                           .count();

    if (buffered_ms >= config_.buffer_ms) {
      frame_data.clear();
      timestamp = it->first;

      for (const auto& packet_info : it->second) {
        frame_data.insert(frame_data.end(), packet_info.packet.payload.begin(),
                          packet_info.packet.payload.end());
      }

      buffer_.erase(it);
      return true;
    }
  }

  return false;
}

uint32_t JitterBuffer::GetBufferedMs() const {
  if (buffer_.empty() || !has_first_packet_) {
    return 0;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - first_packet_time_)
                        .count();
  return static_cast<uint32_t>(elapsed_ms);
}

void JitterBuffer::Reset() {
  buffer_.clear();
  expected_timestamp_ = 0;
  has_expected_timestamp_ = false;
  has_first_packet_ = false;
}

}  // namespace zenremote
