#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "network/connection/base_connection.h"
#include "network/protocol/packet.h"

namespace zenremote {

struct ReceivedPacket {
  RtpHeader header;
  std::vector<uint8_t> payload;
  std::chrono::steady_clock::time_point arrival_time;
};

class RTPReceiver {
 public:
  RTPReceiver();

  std::optional<ReceivedPacket> ReceivePacket(BaseConnection* connection,
                                              int timeout_ms = 1000);

  std::optional<ReceivedPacket> ParsePacket(const uint8_t* buffer,
                                            size_t length);

  std::vector<uint16_t> DetectMissingSequences(uint16_t prev_seq,
                                               uint16_t curr_seq);

  struct Stats {
    uint64_t packets_received = 0;
    uint64_t bytes_received = 0;
    uint64_t packets_lost = 0;
    uint16_t last_sequence_number = 0;
    uint32_t last_timestamp = 0;
  };

  const Stats& GetStats() const { return stats_; }

 private:
  void UpdateStats(const ReceivedPacket& packet);

  Stats stats_;
  uint16_t expected_sequence_number_ = 0;
  bool has_received_first_packet_ = false;
};

}  // namespace zenremote
