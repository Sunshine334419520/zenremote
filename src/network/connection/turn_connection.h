#pragma once

#include <memory>
#include <string>

#include "common/error.h"
#include "network/connection/base_connection.h"
#include "network/io/udp_socket.h"

namespace zenremote {

// Transport layer: TURN relay connection (Phase 2)
// Reuses the same UdpSocket (Network I/O layer) as DirectConnection
class TurnConnection : public BaseConnection {
 public:
  struct Config {
    std::string local_ip = "0.0.0.0";
    uint16_t local_port = 0;
    std::string turn_server_ip;
    uint16_t turn_server_port;
    std::string username;
    std::string password;
    int socket_buffer_size = 1024 * 1024;
    int recv_timeout_ms = 1000;
  };

  TurnConnection();
  ~TurnConnection();

  TurnConnection(const TurnConnection&) = delete;
  TurnConnection& operator=(const TurnConnection&) = delete;

  Result<void> Initialize(const Config& config);
  void Shutdown();

  // BaseConnection interface
  Result<void> Open() override;
  void Close() override;
  bool IsOpen() const override;

  Result<size_t> Send(const uint8_t* data, size_t length) override;
  Result<size_t> Recv(uint8_t* buffer,
                      size_t buffer_size,
                      int timeout_ms) override;

  ConnectionType GetType() const override { return ConnectionType::kRelay; }

 private:
  // TURN protocol methods (Phase 2 implementation)
  Result<void> AllocateRelay();
  Result<void> RefreshAllocation();
  Result<size_t> SendTurnPacket(const uint8_t* data, size_t length);

  Config config_{};
  std::unique_ptr<UdpSocket> socket_;  // Reuses same UDP Socket layer!
  std::string relay_address_;
  uint16_t relay_port_ = 0;
  bool has_allocation_ = false;
};

}  // namespace zenremote
