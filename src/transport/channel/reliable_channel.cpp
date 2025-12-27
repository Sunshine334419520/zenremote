#include "reliable_channel.h"

#include "network/connection/base_connection.h"
#include "network/reliable/reliable_transport.h"

namespace zenremote {

ReliableChannel::ReliableChannel(const std::string& label, const Config& config)
    : label_(label), config_(config) {}

ReliableChannel::~ReliableChannel() = default;

Result<void> ReliableChannel::Send(const uint8_t* data, size_t length) {
  if (state_ != State::kOpen) {
    return Result<void>::Err(ErrorCode::kInvalidOperation,
                             "DataChannel not open");
  }

  if (!transport_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "DataChannel not connected");
  }

  return transport_->Send(data, length);
}

Result<void> ReliableChannel::Send(const std::string& text) {
  return Send(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void ReliableChannel::SetConnection(BaseConnection* connection) {
  if (!connection) {
    transport_.reset();
    state_ = State::kClosed;
    if (on_close_callback_) {
      on_close_callback_();
    }
    return;
  }

  ReliableTransport::Config transport_config;
  transport_config.max_retries = config_.max_retransmits;
  transport_config.ordered = config_.ordered;

  transport_ =
      std::make_unique<ReliableTransport>(connection, transport_config);

  state_ = State::kOpen;
  if (on_open_callback_) {
    on_open_callback_();
  }
}

void ReliableChannel::OnDataReceived(const uint8_t* data, size_t length) {
  if (on_message_callback_) {
    on_message_callback_(data, length);
  }
}

}  // namespace zenremote
