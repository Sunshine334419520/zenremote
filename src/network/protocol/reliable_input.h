#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

#include "network/connection/base_connection.h"
#include "network/protocol/packet.h"
#include "network/protocol/protocol.h"
#include "protocol.h"

namespace zenremote {

// Forward declaration
class DirectConnection;

class ReliableInputSender {
 public:
  static constexpr int kMaxRetries = 3;
  static constexpr int kRetryTimeoutMs = 50;

  explicit ReliableInputSender(uint32_t ssrc, DirectConnection* connection);

  bool SendInputEvent(const InputEvent& event);

  void OnAckMessage(const AckPayload& ack);

  void ProcessRetries();

  struct Stats {
    uint64_t events_sent = 0;
    uint64_t events_acked = 0;
    uint64_t events_retried = 0;
    uint64_t events_failed = 0;
  };

  const Stats& GetStats() const { return stats_; }

 private:
  struct PendingMessage {
    InputEvent event;
    uint16_t sequence_number;
    std::chrono::steady_clock::time_point send_time;
    int retry_count = 0;
  };

  bool SendViaRTP(const InputEvent& event, uint16_t seq);

  uint32_t ssrc_;
  DirectConnection* connection_;
  uint16_t next_sequence_number_ = 0;
  std::queue<PendingMessage> pending_messages_;
  Stats stats_;
};

class ReliableInputReceiver {
 public:
  using InputEventCallback = void (*)(const InputEvent& event, void* user_data);

  explicit ReliableInputReceiver(DirectConnection* connection);

  void SetCallback(InputEventCallback callback, void* user_data);

  void OnControlMessage(const uint8_t* payload, size_t length);

 private:
  void SendAck(uint16_t seq);

  DirectConnection* connection_;
  InputEventCallback callback_ = nullptr;
  void* user_data_ = nullptr;
  uint16_t last_acked_sequence_ = 0;
  uint16_t ack_sequence_number_ = 0;
  uint32_t ssrc_ = 0;
};

}  // namespace zenremote
