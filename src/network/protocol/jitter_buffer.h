#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "packet.h"

namespace zenremote {

class JitterBuffer {
 public:
  struct Config {
    uint32_t buffer_ms = 50;
    uint32_t max_packets = 100;
  };

  explicit JitterBuffer(const Config& config);

  void InsertPacket(const RtpPacket& packet);

  bool TryExtractFrame(std::vector<uint8_t>& frame_data, uint32_t& timestamp);

  uint32_t GetBufferedMs() const;

  void Reset();

 private:
  struct PacketInfo {
    RtpPacket packet;
    std::chrono::steady_clock::time_point insert_time;
  };

  Config config_;
  std::map<uint32_t, std::vector<PacketInfo>> buffer_;
  uint32_t expected_timestamp_ = 0;
  bool has_expected_timestamp_ = false;
  std::chrono::steady_clock::time_point first_packet_time_;
  bool has_first_packet_ = false;
};

}  // namespace zenremote
