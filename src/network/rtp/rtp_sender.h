#pragma once

#include <cstdint>
#include <memory>

#include "common/error.h"

namespace zenremote {

class BaseConnection;

/**
 * @brief RTP 发送器
 */
class RTPSender {
 public:
  struct Config {
    uint32_t ssrc = 0;
    uint8_t payload_type = 96;
    uint32_t clock_rate = 90000;
  };

  RTPSender(BaseConnection* connection, const Config& config);
  ~RTPSender();

  Result<void> SendVideoFrame(const uint8_t* data,
                              size_t length,
                              uint32_t timestamp,
                              bool is_last_packet);

  Result<void> SendAudioPacket(const uint8_t* data,
                               size_t length,
                               uint32_t timestamp);

  struct Stats {
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
  };

  const Stats& GetStats() const { return stats_; }

 private:
  BaseConnection* connection_;
  Config config_;
  uint16_t sequence_number_ = 0;
  Stats stats_;
};

}  // namespace zenremote
