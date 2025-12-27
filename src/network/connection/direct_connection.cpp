#include "network/connection/direct_connection.h"

#include "common/log_manager.h"

namespace zenremote {

DirectConnection::DirectConnection() = default;

DirectConnection::~DirectConnection() {
  Shutdown();
}

Result<void> DirectConnection::Open() {
  return Initialize(config_);
}

void DirectConnection::Close() {
  Shutdown();
}

Result<void> DirectConnection::Initialize(const Config& config) {
  if (IsOpen()) {
    return Result<void>::Err(ErrorCode::kAlreadyRunning,
                             "DirectConnection already initialized");
  }

  config_ = config;

  // Create UdpSocket (Network I/O layer)
  UdpSocket::Config socket_config;
  socket_config.local_ip = config.local_ip;
  socket_config.local_port = config.local_port;
  socket_config.socket_buffer_size = config.socket_buffer_size;
  socket_config.recv_timeout_ms = static_cast<int>(config.recv_timeout.count());

  socket_ = std::make_unique<UdpSocket>(socket_config);

  if (!socket_->Open()) {
    socket_.reset();
    return Result<void>::Err(ErrorCode::kNetworkError,
                             "Failed to open UDP socket");
  }

  // Socket is now open and ready to use

  // Set remote endpoint if configured
  if (!config.remote.address.empty() && config.remote.port > 0) {
    auto remote_result = SetRemote(config.remote);
    if (remote_result.IsErr()) {
      ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Failed to set remote endpoint: {}",
                     remote_result.Message());
    }
  }

  ZENREMOTE_INFO(LOG_MODULE_NETWORK,
                 "DirectConnection initialized: local={}:{}, remote={}:{}",
                 config_.local_ip, config_.local_port, config_.remote.address,
                 config_.remote.port);
  return Result<void>::Ok();
}

void DirectConnection::Shutdown() {
  if (socket_) {
    socket_->Close();
    socket_.reset();
  }
  has_remote_endpoint_ = false;
  ZENREMOTE_DEBUG(LOG_MODULE_NETWORK, "DirectConnection shutdown");
}

bool DirectConnection::IsOpen() const {
  return socket_ && socket_->IsOpen();
}

Result<void> DirectConnection::SetRemote(const Endpoint& endpoint) {
  if (endpoint.address.empty() || endpoint.port == 0) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid remote endpoint");
  }

  remote_endpoint_ = endpoint;
  has_remote_endpoint_ = true;

  ZENREMOTE_DEBUG(LOG_MODULE_NETWORK, "Remote endpoint set: {}:{}",
                  endpoint.address, endpoint.port);
  return Result<void>::Ok();
}

Result<size_t> DirectConnection::Recv(uint8_t* buffer,
                                      size_t buffer_size,
                                      int timeout_ms) {
  return Receive(buffer, buffer_size, std::chrono::milliseconds(timeout_ms));
}

Result<size_t> DirectConnection::Send(const uint8_t* data, size_t length) {
  if (!IsOpen()) {
    return Result<size_t>::Err(ErrorCode::kNotInitialized,
                               "DirectConnection not initialized");
  }

  if (!data || length == 0) {
    return Result<size_t>::Err(ErrorCode::kInvalidParameter,
                               "Invalid send parameters");
  }

  if (!has_remote_endpoint_) {
    return Result<size_t>::Err(ErrorCode::kNetworkError,
                               "Remote endpoint not set");
  }

  // Use UdpSocket layer to send
  if (!socket_->SendTo(data, length, remote_endpoint_.address,
                       remote_endpoint_.port)) {
    send_failures_++;
    return Result<size_t>::Err(ErrorCode::kNetworkError, "Send failed");
  }

  bytes_sent_ += length;
  packets_sent_++;
  return Result<size_t>::Ok(length);
}

Result<size_t> DirectConnection::Receive(uint8_t* buffer, size_t buffer_size) {
  return Receive(buffer, buffer_size, config_.recv_timeout);
}

Result<size_t> DirectConnection::Receive(uint8_t* buffer,
                                         size_t buffer_size,
                                         std::chrono::milliseconds timeout) {
  if (!IsOpen()) {
    return Result<size_t>::Err(ErrorCode::kNotInitialized,
                               "DirectConnection not initialized");
  }

  if (!buffer || buffer_size == 0) {
    return Result<size_t>::Err(ErrorCode::kInvalidParameter,
                               "Invalid receive parameters");
  }

  // Use UdpSocket layer to receive
  std::string from_ip;
  uint16_t from_port;
  size_t received = buffer_size;

  if (!socket_->RecvFrom(buffer, received, from_ip, from_port,
                         static_cast<int>(timeout.count()))) {
    recv_failures_++;
    return Result<size_t>::Err(ErrorCode::kTimeout, "Receive timeout or error");
  }

  bytes_received_ += received;
  packets_received_++;
  return Result<size_t>::Ok(received);
}

DirectConnection::Stats DirectConnection::GetStats() const {
  Stats stats;
  stats.bytes_sent = bytes_sent_.load();
  stats.bytes_received = bytes_received_.load();
  stats.packets_sent = packets_sent_.load();
  stats.packets_received = packets_received_.load();
  stats.send_failures = send_failures_.load();
  stats.recv_failures = recv_failures_.load();
  return stats;
}

}  // namespace zenremote
