#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/error.h"
#include "network/connection/base_connection.h"
#include "network/io/udp_socket.h"

namespace zenremote {

/**
 * @brief 网络端点 (IP + Port)
 */
struct Endpoint {
  std::string address;  ///< IP 地址
  uint16_t port = 0;    ///< 端口号
};

/**
 * @brief 传输层实现 - 局域网直连 (Phase 1)
 *
 * 职责：
 * - 管理固定的远程端点 (IP + Port)
 * - 使用 UdpSocket 进行实际 UDP I/O
 * - 实现 BaseConnection 接口
 *
 * 设计理念：
 * - 不直接操作 socket，委托给 UdpSocket (网络 I/O 层)
 * - 提供统一的 Send/Recv 接口给上层
 * - Phase 1 使用，Phase 2 无需修改
 *
 * 使用场景：
 * - 局域网内设备直连
 * - 已知对方 IP 和端口
 * - 低延迟场景 (< 5ms)
 */
class DirectConnection : public BaseConnection {
 public:
  /**
   * @brief 直连配置
   */
  struct Config {
    std::string local_ip = "0.0.0.0";      ///< 本地 IP (0.0.0.0 = 所有接口)
    uint16_t local_port = 0;               ///< 本地端口 (0 = 自动分配)
    Endpoint remote;                       ///< 远程端点 (IP + Port)
    int socket_buffer_size = 1024 * 1024;  ///< Socket 缓冲区大小
    std::chrono::milliseconds recv_timeout{1000};  ///< 接收超时
  };

  /**
   * @brief 连接统计信息
   */
  struct Stats {
    uint64_t bytes_sent = 0;        ///< 发送字节总数
    uint64_t bytes_received = 0;    ///< 接收字节总数
    uint64_t packets_sent = 0;      ///< 发送包总数
    uint64_t packets_received = 0;  ///< 接收包总数
    uint64_t send_failures = 0;     ///< 发送失败次数
    uint64_t recv_failures = 0;     ///< 接收失败次数
  };

  DirectConnection();
  ~DirectConnection();

  DirectConnection(const DirectConnection&) = delete;
  DirectConnection& operator=(const DirectConnection&) = delete;

  Result<void> Initialize(const Config& config);
  void Shutdown();

  Result<void> Open() override;
  void Close() override;
  bool IsOpen() const override;
  bool IsInitialized() const { return socket_ != nullptr; }

  Result<size_t> Send(const uint8_t* data, size_t length) override;
  Result<size_t> Send(const std::vector<uint8_t>& payload) {
    return Send(payload.data(), payload.size());
  }

  Result<size_t> Recv(uint8_t* buffer,
                      size_t buffer_size,
                      int timeout_ms) override;

  Result<size_t> Receive(uint8_t* buffer, size_t buffer_size);
  Result<size_t> Receive(uint8_t* buffer,
                         size_t buffer_size,
                         std::chrono::milliseconds timeout);

  ConnectionType GetType() const override { return ConnectionType::kDirect; }

  Result<void> SetRemote(const Endpoint& endpoint);
  Stats GetStats() const;
  const Config& config() const { return config_; }

 private:
  Config config_{};
  std::unique_ptr<UdpSocket> socket_;
  Endpoint remote_endpoint_;
  bool has_remote_endpoint_ = false;

  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> bytes_received_{0};
  std::atomic<uint64_t> packets_sent_{0};
  std::atomic<uint64_t> packets_received_{0};
  std::atomic<uint64_t> send_failures_{0};
  std::atomic<uint64_t> recv_failures_{0};
};

}  // namespace zenremote
