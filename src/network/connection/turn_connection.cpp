#include "network/connection/turn_connection.h"

#include "common/log_manager.h"

namespace zenremote {

TurnConnection::TurnConnection() = default;

TurnConnection::~TurnConnection() {
  Shutdown();
}

Result<void> TurnConnection::Initialize(const Config& config) {
  if (socket_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "TurnConnection already initialized");
  }

  config_ = config;

  // Create UdpSocket (Network I/O layer)
  UdpSocket::Config socket_config;
  socket_config.local_ip = config.local_ip;
  socket_config.local_port = config.local_port;
  socket_config.socket_buffer_size = config.socket_buffer_size;
  socket_config.recv_timeout_ms = config.recv_timeout_ms;

  socket_ = std::make_unique<UdpSocket>(socket_config);

  if (!socket_->Open()) {
    socket_.reset();
    return Result<void>::Err(ErrorCode::kSocketError,
                             "Failed to open UDP socket");
  }

  ZENREMOTE_INFO(LOG_MODULE_NETWORK,
                 "TurnConnection initialized: local={}:{}, turn={}:{}",
                 config_.local_ip, config_.local_port, config_.turn_server_ip,
                 config_.turn_server_port);
  return Result<void>::Ok();
}

void TurnConnection::Shutdown() {
  if (socket_) {
    socket_->Close();
    socket_.reset();
  }
  has_allocation_ = false;
  ZENREMOTE_DEBUG(LOG_MODULE_NETWORK, "TurnConnection shutdown");
}

Result<void> TurnConnection::Open() {
  if (!socket_ || !socket_->IsOpen()) {
    return Result<void>::Err(ErrorCode::kSocketError, "Socket not open");
  }

  // Phase 2: Allocate relay address from TURN server
  auto result = AllocateRelay();
  if (result.IsErr()) {
    return result;
  }

  return Result<void>::Ok();
}

void TurnConnection::Close() {
  Shutdown();
}

bool TurnConnection::IsOpen() const {
  return socket_ && socket_->IsOpen() && has_allocation_;
}

Result<size_t> TurnConnection::Send(const uint8_t* data, size_t length) {
  if (!IsOpen()) {
    return Result<size_t>::Err(ErrorCode::kSocketError,
                               "TurnConnection not open");
  }

  if (!data || length == 0) {
    return Result<size_t>::Err(ErrorCode::kInvalidParameter,
                               "Invalid send parameters");
  }

  return SendTurnPacket(data, length);
}

Result<size_t> TurnConnection::Recv(uint8_t* buffer,
                                    size_t buffer_size,
                                    int timeout_ms) {
  if (!IsOpen()) {
    return Result<size_t>::Err(ErrorCode::kSocketError,
                               "TurnConnection not open");
  }

  if (!buffer || buffer_size == 0) {
    return Result<size_t>::Err(ErrorCode::kInvalidParameter,
                               "Invalid receive parameters");
  }

  std::string from_ip;
  uint16_t from_port;
  size_t received = buffer_size;

  if (!socket_->RecvFrom(buffer, received, from_ip, from_port, timeout_ms)) {
    return Result<size_t>::Err(ErrorCode::kSocketError,
                               "Receive timeout or error");
  }

  return Result<size_t>::Ok(received);
}

Result<void> TurnConnection::AllocateRelay() {
  // Phase 2: Send TURN Allocate request
  // TODO: Implement TURN protocol
  has_allocation_ = true;
  ZENREMOTE_INFO(LOG_MODULE_NETWORK, "TURN relay allocated: {}:{}",
                 relay_address_, relay_port_);
  return Result<void>::Ok();
}

Result<void> TurnConnection::RefreshAllocation() {
  // Phase 2: Send TURN Refresh request
  // TODO: Implement TURN protocol
  return Result<void>::Ok();
}

Result<size_t> TurnConnection::SendTurnPacket(const uint8_t* data,
                                              size_t length) {
  // Phase 2: Wrap data in TURN Send indication
  // TODO: Implement TURN protocol

  // For now, directly send to TURN server
  if (!socket_->SendTo(data, length, config_.turn_server_ip,
                       config_.turn_server_port)) {
    return Result<size_t>::Err(ErrorCode::kSocketSendFailed, "Send failed");
  }

  return Result<size_t>::Ok(length);
}

}  // namespace zenremote
