#pragma once

#include <cstdint>
#include <vector>

#include "network/connection/base_connection.h"
#include "network/protocol/protocol.h"
#include "network/protocol/rtp_receiver.h"
#include "network/protocol/rtp_sender.h"

namespace zenremote {

enum class HandshakeState {
  kIdle,
  kRequestSent,
  kResponseReceived,
  kCompleted,
  kFailed,
};

class HandshakeManager {
 public:
  explicit HandshakeManager(uint32_t ssrc, BaseConnection* connection);

  bool InitiateHandshake(uint32_t session_id);

  bool WaitForHandshakeResponse(int timeout_ms = 3000);

  bool SendHandshakeResponse(uint32_t session_id, uint32_t remote_ssrc);

  bool WaitForHandshakeRequest(int timeout_ms = 5000);

  HandshakeState GetState() const { return state_; }
  bool IsCompleted() const { return state_ == HandshakeState::kCompleted; }

  uint32_t GetRemoteSSRC() const { return remote_ssrc_; }
  uint32_t GetSessionID() const { return session_id_; }

 private:
  bool SendHandshake(ControlMessageType type, const HandshakePayload& payload);
  std::optional<ControlMessage> ReceiveControlMessage(int timeout_ms);

  uint32_t ssrc_;
  BaseConnection* connection_;
  std::unique_ptr<RTPSender> rtp_sender_;
  std::unique_ptr<RTPReceiver> rtp_receiver_;

  HandshakeState state_ = HandshakeState::kIdle;
  uint32_t session_id_ = 0;
  uint32_t remote_ssrc_ = 0;
};

}  // namespace zenremote
