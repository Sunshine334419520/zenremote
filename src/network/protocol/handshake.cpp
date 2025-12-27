#include "network/protocol/handshake.h"

#include <random>

#include "common/log_manager.h"

namespace zenremote {

HandshakeManager::HandshakeManager(uint32_t ssrc, BaseConnection* connection)
    : ssrc_(ssrc), connection_(connection) {
  rtp_sender_ = std::make_unique<RTPSender>(ssrc, connection);
  rtp_receiver_ = std::make_unique<RTPReceiver>();
}

bool HandshakeManager::InitiateHandshake(uint32_t session_id) {
  if (state_ != HandshakeState::kIdle) {
    ZENREMOTE_ERROR("Handshake already in progress or completed");
    return false;
  }

  session_id_ = session_id;

  HandshakePayload payload;
  payload.version = kProtocolVersion;
  payload.session_id = session_id;
  payload.ssrc = ssrc_;
  payload.supported_codecs = 0x03;
  payload.capabilities_flags = 0x0007;

  if (!SendHandshake(ControlMessageType::kHandshake, payload)) {
    state_ = HandshakeState::kFailed;
    return false;
  }

  state_ = HandshakeState::kRequestSent;
  ZENREMOTE_INFO("Handshake request sent: session_id=0x{:08X}, ssrc=0x{:08X}",
                 session_id, ssrc_);
  return true;
}

bool HandshakeManager::WaitForHandshakeResponse(int timeout_ms) {
  if (state_ != HandshakeState::kRequestSent) {
    ZENREMOTE_ERROR("Invalid state for waiting response");
    return false;
  }

  auto ctrl_msg = ReceiveControlMessage(timeout_ms);
  if (!ctrl_msg.has_value()) {
    ZENREMOTE_ERROR("Failed to receive handshake response");
    state_ = HandshakeState::kFailed;
    return false;
  }

  if (ctrl_msg->type != ControlMessageType::kHandshakeAck) {
    ZENREMOTE_ERROR("Expected handshake ACK, got type {}",
                    static_cast<int>(ctrl_msg->type));
    state_ = HandshakeState::kFailed;
    return false;
  }

  auto response_payload =
      ParseHandshake(ctrl_msg->payload.data(), ctrl_msg->payload.size());
  if (!response_payload.has_value()) {
    ZENREMOTE_ERROR("Failed to parse handshake response");
    state_ = HandshakeState::kFailed;
    return false;
  }

  if (response_payload->session_id != session_id_) {
    ZENREMOTE_ERROR("Session ID mismatch");
    state_ = HandshakeState::kFailed;
    return false;
  }

  remote_ssrc_ = response_payload->ssrc;
  state_ = HandshakeState::kCompleted;

  ZENREMOTE_INFO("Handshake completed: remote_ssrc=0x{:08X}", remote_ssrc_);
  return true;
}

bool HandshakeManager::SendHandshakeResponse(uint32_t session_id,
                                             uint32_t remote_ssrc) {
  session_id_ = session_id;
  remote_ssrc_ = remote_ssrc;

  HandshakePayload payload;
  payload.version = kProtocolVersion;
  payload.session_id = session_id;
  payload.ssrc = ssrc_;
  payload.supported_codecs = 0x03;
  payload.capabilities_flags = 0x0007;

  if (!SendHandshake(ControlMessageType::kHandshakeAck, payload)) {
    state_ = HandshakeState::kFailed;
    return false;
  }

  state_ = HandshakeState::kCompleted;
  ZENREMOTE_INFO("Handshake response sent and completed");
  return true;
}

bool HandshakeManager::WaitForHandshakeRequest(int timeout_ms) {
  if (state_ != HandshakeState::kIdle) {
    ZENREMOTE_ERROR("Invalid state for waiting request");
    return false;
  }

  auto ctrl_msg = ReceiveControlMessage(timeout_ms);
  if (!ctrl_msg.has_value()) {
    ZENREMOTE_ERROR("Failed to receive handshake request");
    return false;
  }

  if (ctrl_msg->type != ControlMessageType::kHandshake) {
    ZENREMOTE_ERROR("Expected handshake request, got type {}",
                    static_cast<int>(ctrl_msg->type));
    return false;
  }

  auto request_payload =
      ParseHandshake(ctrl_msg->payload.data(), ctrl_msg->payload.size());
  if (!request_payload.has_value()) {
    ZENREMOTE_ERROR("Failed to parse handshake request");
    return false;
  }

  ZENREMOTE_INFO(
      "Handshake request received: session_id=0x{:08X}, "
      "remote_ssrc=0x{:08X}",
      request_payload->session_id, request_payload->ssrc);

  return SendHandshakeResponse(request_payload->session_id,
                               request_payload->ssrc);
}

bool HandshakeManager::SendHandshake(ControlMessageType type,
                                     const HandshakePayload& payload) {
  auto handshake_data = SerializeHandshake(payload);

  ControlMessage ctrl_msg;
  ctrl_msg.type = type;
  ctrl_msg.sequence = 0;
  ctrl_msg.timestamp_ms = GetTimestampMs();
  ctrl_msg.payload = handshake_data;

  auto ctrl_data = SerializeControlMessage(ctrl_msg);

  return rtp_sender_->SendControlMessage(ctrl_data.data(), ctrl_data.size(),
                                         ctrl_msg.timestamp_ms);
}

std::optional<ControlMessage> HandshakeManager::ReceiveControlMessage(
    int timeout_ms) {
  auto received_packet = rtp_receiver_->ReceivePacket(connection_, timeout_ms);
  if (!received_packet.has_value()) {
    return std::nullopt;
  }

  if (received_packet->header.payload_type != PayloadType::kControl) {
    ZENREMOTE_WARN("Received non-control packet during handshake");
    return std::nullopt;
  }

  return ParseControlMessage(received_packet->payload.data(),
                             received_packet->payload.size());
}

}  // namespace zenremote
