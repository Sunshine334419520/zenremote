#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace zenremote {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

/**
 * @brief 网络 I/O 层 - 纯 UDP Socket 封装
 *
 * 职责：
 * - 提供跨平台的 UDP Socket 操作接口
 * - 处理平台差异 (Windows Winsock2 vs POSIX BSD sockets)
 * - 错误处理和日志记录
 * - 统计信息收集
 *
 * 不负责：
 * - 不知道远程端点是谁 (SendTo 每次指定)
 * - 不知道连接状态 (UDP 无连接)
 * - 不处理协议逻辑 (RTP/RTCP/TURN 等)
 *
 * 设计理念：
 * - 所有传输层 (DirectConnection/TurnConnection) 复用此类
 * - 避免重复实现 socket 操作
 * - 单一职责：只做 UDP I/O
 */
class UdpSocket {
 public:
  /**
   * @brief Socket 配置
   */
  struct Config {
    std::string local_ip;                  ///< 本地绑定 IP (0.0.0.0 = 所有接口)
    uint16_t local_port = 0;               ///< 本地绑定端口 (0 = 自动分配)
    int socket_buffer_size = 1024 * 1024;  ///< Socket 缓冲区大小 (默认 1MB)
    int recv_timeout_ms = 1000;            ///< 接收超时时间 (毫秒)
  };

  /**
   * @brief Socket 统计信息
   */
  struct Stats {
    uint64_t bytes_sent = 0;        ///< 发送字节总数
    uint64_t bytes_received = 0;    ///< 接收字节总数
    uint64_t packets_sent = 0;      ///< 发送包总数
    uint64_t packets_received = 0;  ///< 接收包总数
  };

  explicit UdpSocket(const Config& config);
  ~UdpSocket();

  /**
   * @brief 打开 Socket (创建、绑定、设置选项)
   * @return 成功返回 true，失败返回 false
   */
  bool Open();

  /**
   * @brief 关闭 Socket
   */
  void Close();

  /**
   * @brief 检查 Socket 是否已打开
   */
  bool IsOpen() const { return socket_fd_ != kInvalidSocket; }

  /**
   * @brief 发送数据到指定地址
   * @param data 数据指针
   * @param length 数据长度
   * @param ip 目标 IP 地址
   * @param port 目标端口
   * @return 成功返回 true，失败返回 false
   */
  bool SendTo(const uint8_t* data,
              size_t length,
              const std::string& ip,
              uint16_t port);

  /**
   * @brief 从任意地址接收数据
   * @param buffer 接收缓冲区
   * @param length 输入：缓冲区大小，输出：实际接收字节数
   * @param from_ip 输出：发送方 IP 地址
   * @param from_port 输出：发送方端口
   * @param timeout_ms 超时时间 (毫秒)，-1 表示使用配置的默认超时
   * @return 成功返回 true，超时或失败返回 false
   */
  bool RecvFrom(uint8_t* buffer,
                size_t& length,
                std::string& from_ip,
                uint16_t& from_port,
                int timeout_ms = -1);

  /**
   * @brief 等待 Socket 可读 (使用 select)
   * @param timeout_ms 超时时间 (毫秒)
   * @return 可读返回 true，超时或失败返回 false
   */
  bool WaitForRead(int timeout_ms);

  /**
   * @brief 获取统计信息
   */
  const Stats& GetStats() const { return stats_; }

  /**
   * @brief 获取原始 socket 句柄 (高级用途)
   */
  socket_t GetHandle() const { return socket_fd_; }

 private:
  Config config_;
  socket_t socket_fd_ = kInvalidSocket;
  Stats stats_;

  bool CreateSocket();
  bool BindSocket();
  bool SetSocketOptions();
  std::string GetLastErrorString() const;
};

}  // namespace zenremote
