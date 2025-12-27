#include "reliable_transport.h"

#include "network/connection/base_connection.h"

namespace zenremote {

ReliableTransport::ReliableTransport(BaseConnection* connection,
                                     const Config& config)
    : connection_(connection), config_(config) {}

ReliableTransport::~ReliableTransport() = default;

Result<void> ReliableTransport::Send(const uint8_t* data, size_t length) {
  if (!connection_) {
    return Result<void>::Err(ErrorCode::kNetworkError, "No connection");
  }

  // TODO: 添加序列号、ACK 机制
  auto send_result = connection_->Send(data, length);
  if (!send_result.IsOk()) {
    return Result<void>::Err(send_result.Code(), send_result.Message());
  }
  return Result<void>::Ok();
}

void ReliableTransport::OnAckReceived(uint32_t sequence_number) {
  // TODO: 处理 ACK
}

}  // namespace zenremote
