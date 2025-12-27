#include "network/protocol/reliable_input.h"

#include "common/log_manager.h"
#include "network/connection/direct_connection.h"

namespace zenremote {

ReliableInputSender::ReliableInputSender(uint32_t ssrc,
                                         DirectConnection* connection)
    : ssrc_(ssrc), connection_(connection) {}

bool ReliableInputSender::SendInputEvent(const InputEvent& event) {
  if (!connection_ || !connection_->IsInitialized()) {
    ZENREMOTE_ERROR("Connection not initialized");
    return false;
  }

  PendingMessage msg;
  msg.event = event;
  msg.sequence_number = next_sequence_number_++;
  msg.send_time = std::chrono::steady_clock::now();
  msg.retry_count = 0;

  if (!SendViaRTP(event, msg.sequence_number)) {
    ZENREMOTE_ERROR("Failed to send input event");
    return false;
  }

  pending_messages_.push(msg);
  stats_.events_sent++;

  ZENREMOTE_DEBUG("Input event sent: type={}, seq={}",
                  static_cast<int>(event.type), msg.sequence_number);
  return true;
}

void ReliableInputSender::OnAckMessage(const AckPayload& ack) {
  while (!pending_messages_.empty()) {
    auto& front = pending_messages_.front();
    if (front.sequence_number == ack.acked_sequence) {
      auto now = std::chrono::steady_clock::now();
      auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - front.send_time)
                        .count();
      ZENREMOTE_DEBUG("Input ACK received: seq={}, RTT={}ms",
                      ack.acked_sequence, rtt_ms);
      stats_.events_acked++;
      pending_messages_.pop();
      break;
    } else if (front.sequence_number < ack.acked_sequence) {
      pending_messages_.pop();
    } else {
      break;
    }
  }
}

void ReliableInputSender::ProcessRetries() {
  auto now = std::chrono::steady_clock::now();
  std::queue<PendingMessage> remaining;

  while (!pending_messages_.empty()) {
    auto msg = pending_messages_.front();
    pending_messages_.pop();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - msg.send_time)
                          .count();

    if (elapsed_ms >= kRetryTimeoutMs) {
      if (msg.retry_count < kMaxRetries) {
        msg.retry_count++;
        msg.send_time = now;
        if (SendViaRTP(msg.event, msg.sequence_number)) {
          ZENREMOTE_WARN("Retrying input event: seq={}, attempt={}",
                         msg.sequence_number, msg.retry_count);
          stats_.events_retried++;
          remaining.push(msg);
        } else {
          ZENREMOTE_ERROR("Retry send failed: seq={}", msg.sequence_number);
          stats_.events_failed++;
        }
      } else {
        ZENREMOTE_ERROR("Input event failed after {} retries: seq={}",
                        kMaxRetries, msg.sequence_number);
        stats_.events_failed++;
      }
    } else {
      remaining.push(msg);
    }
  }

  pending_messages_ = remaining;
}

bool ReliableInputSender::SendViaRTP(const InputEvent& event, uint16_t seq) {
  auto event_payload = SerializeInputEvent(event);

  ControlMessage ctrl_msg;
  ctrl_msg.type = ControlMessageType::kInputEvent;
  ctrl_msg.sequence = seq;
  ctrl_msg.timestamp_ms = GetTimestampMs();
  ctrl_msg.payload = event_payload;

  auto ctrl_payload = SerializeControlMessage(ctrl_msg);

  RtpPacket packet;
  packet.header.version = kRtpVersion;
  packet.header.payload_type = PayloadType::kControl;
  packet.header.sequence_number = seq;
  packet.header.timestamp = ctrl_msg.timestamp_ms;
  packet.header.ssrc = ssrc_;
  packet.header.marker = false;
  packet.payload = ctrl_payload;

  auto buffer = SerializeRtpPacket(packet);
  if (buffer.empty()) {
    return false;
  }

  auto result = connection_->Send(buffer);
  return result.IsOk();
}

ReliableInputReceiver::ReliableInputReceiver(DirectConnection* connection)
    : connection_(connection) {}

void ReliableInputReceiver::SetCallback(InputEventCallback callback,
                                        void* user_data) {
  callback_ = callback;
  user_data_ = user_data;
}

void ReliableInputReceiver::OnControlMessage(const uint8_t* payload,
                                             size_t length) {
  auto ctrl_msg = ParseControlMessage(payload, length);
  if (!ctrl_msg.has_value()) {
    ZENREMOTE_WARN("Failed to parse control message");
    return;
  }

  if (ctrl_msg->type == ControlMessageType::kInputEvent) {
    auto event =
        ParseInputEvent(ctrl_msg->payload.data(), ctrl_msg->payload.size());
    if (!event.has_value()) {
      ZENREMOTE_WARN("Failed to parse input event");
      return;
    }

    if (callback_) {
      callback_(event.value(), user_data_);
    }

    SendAck(ctrl_msg->sequence);

    ZENREMOTE_DEBUG("Input event applied: type={}, seq={}",
                    static_cast<int>(event->type), ctrl_msg->sequence);
  }
}

void ReliableInputReceiver::SendAck(uint16_t seq) {
  AckPayload ack;
  ack.acked_sequence = seq;
  ack.original_timestamp_ms = GetTimestampMs();

  auto ack_payload = SerializeAckPayload(ack);

  ControlMessage ctrl_msg;
  ctrl_msg.type = ControlMessageType::kInputAck;
  ctrl_msg.sequence = ack_sequence_number_++;
  ctrl_msg.timestamp_ms = ack.original_timestamp_ms;
  ctrl_msg.payload = ack_payload;

  auto ctrl_payload = SerializeControlMessage(ctrl_msg);

  RtpPacket packet;
  packet.header.version = kRtpVersion;
  packet.header.payload_type = PayloadType::kControlAck;
  packet.header.sequence_number = ctrl_msg.sequence;
  packet.header.timestamp = ctrl_msg.timestamp_ms;
  packet.header.ssrc = ssrc_;
  packet.header.marker = false;
  packet.payload = ctrl_payload;

  auto buffer = SerializeRtpPacket(packet);
  if (!buffer.empty()) {
    connection_->Send(buffer);
  }
}

}  // namespace zenremote
