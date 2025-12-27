#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "network/connection/base_connection.h"
#include "network/protocol/packet.h"

namespace zenremote {

class RTPSender {
 public:
  explicit RTPSender(uint32_t ssrc, BaseConnection* connection);

  bool SendVideoFrame(const uint8_t* data,
                      size_t length,
                      uint32_t timestamp_90khz,
                      bool marker = false);

  bool SendAudioPacket(const uint8_t* data,
                       size_t length,
                       uint32_t timestamp_48khz);

  bool SendControlMessage(const uint8_t* data,
                          size_t length,
                          uint32_t timestamp_ms);

  bool SendRawRtpPacket(const RtpPacket& packet);

  struct Stats {
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
    uint16_t last_sequence_number = 0;
  };

  const Stats& GetStats() const { return stats_; }

 private:
  RtpHeader BuildHeader(PayloadType payload_type,
                        uint16_t seq,
                        uint32_t timestamp,
                        bool marker);

  uint32_t ssrc_;
  BaseConnection* connection_;
  uint16_t video_sequence_number_ = 0;
  uint16_t audio_sequence_number_ = 0;
  uint16_t control_sequence_number_ = 0;
  Stats stats_;
};

}  // namespace zenremote
