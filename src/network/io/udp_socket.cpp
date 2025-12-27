#include "network/io/udp_socket.h"

#include <cstring>

#include "common/log_manager.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace zenremote {

UdpSocket::UdpSocket(const Config& config) : config_(config) {}

UdpSocket::~UdpSocket() {
  Close();
}

bool UdpSocket::Open() {
  if (IsOpen()) {
    ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Socket already opened");
    return true;
  }

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "WSAStartup failed: {}",
                    GetLastErrorString());
    return false;
  }
#endif

  if (!CreateSocket()) {
    return false;
  }

  if (!BindSocket()) {
    Close();
    return false;
  }

  if (!SetSocketOptions()) {
    Close();
    return false;
  }

  ZENREMOTE_INFO(LOG_MODULE_NETWORK, "UDP Socket opened: {}:{}",
                 config_.local_ip, config_.local_port);
  return true;
}

void UdpSocket::Close() {
  if (!IsOpen()) {
    return;
  }

#ifdef _WIN32
  closesocket(socket_fd_);
  WSACleanup();
#else
  close(socket_fd_);
#endif

  socket_fd_ = kInvalidSocket;
  ZENREMOTE_INFO(LOG_MODULE_NETWORK, "UDP Socket closed");
}

bool UdpSocket::SendTo(const uint8_t* data,
                       size_t length,
                       const std::string& ip,
                       uint16_t port) {
  if (!IsOpen()) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "Socket not opened");
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

  int sent = sendto(socket_fd_, reinterpret_cast<const char*>(data), length, 0,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

  if (sent < 0) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "sendto failed: {}",
                    GetLastErrorString());
    return false;
  }

  stats_.bytes_sent += sent;
  stats_.packets_sent++;
  return true;
}

bool UdpSocket::RecvFrom(uint8_t* buffer,
                         size_t& length,
                         std::string& from_ip,
                         uint16_t& from_port,
                         int timeout_ms) {
  if (!IsOpen()) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "Socket not opened");
    return false;
  }

  if (timeout_ms >= 0 && !WaitForRead(timeout_ms)) {
    return false;
  }

  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);

  int received = recvfrom(socket_fd_, reinterpret_cast<char*>(buffer), length,
                          0, reinterpret_cast<sockaddr*>(&addr), &addr_len);

  if (received < 0) {
#ifdef _WIN32
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAETIMEDOUT) {
      return false;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return false;
    }
#endif
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "recvfrom failed: {}",
                    GetLastErrorString());
    return false;
  }

  length = received;
  stats_.bytes_received += received;
  stats_.packets_received++;

  // Extract sender address
  char ip_buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, sizeof(ip_buffer));
  from_ip = ip_buffer;
  from_port = ntohs(addr.sin_port);

  return true;
}

bool UdpSocket::WaitForRead(int timeout_ms) {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(socket_fd_, &read_fds);

  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int ret = select(static_cast<int>(socket_fd_) + 1, &read_fds, nullptr,
                   nullptr, &tv);
  if (ret < 0) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "select failed: {}",
                    GetLastErrorString());
    return false;
  }

  return ret > 0;
}

bool UdpSocket::CreateSocket() {
  socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_fd_ == kInvalidSocket) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "socket() failed: {}",
                    GetLastErrorString());
    return false;
  }
  return true;
}

bool UdpSocket::BindSocket() {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config_.local_port);

  if (config_.local_ip.empty() || config_.local_ip == "0.0.0.0") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    inet_pton(AF_INET, config_.local_ip.c_str(), &addr.sin_addr);
  }

  if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ZENREMOTE_ERROR(LOG_MODULE_NETWORK, "bind failed: {}",
                    GetLastErrorString());
    return false;
  }

  return true;
}

bool UdpSocket::SetSocketOptions() {
  // Set send/recv buffer size
  int buffer_size = config_.socket_buffer_size;
  if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF,
                 reinterpret_cast<const char*>(&buffer_size),
                 sizeof(buffer_size)) < 0) {
    ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Failed to set SO_SNDBUF: {}",
                   GetLastErrorString());
  }

  if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<const char*>(&buffer_size),
                 sizeof(buffer_size)) < 0) {
    ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Failed to set SO_RCVBUF: {}",
                   GetLastErrorString());
  }

  // Set receive timeout
  if (config_.recv_timeout_ms > 0) {
#ifdef _WIN32
    DWORD timeout = config_.recv_timeout_ms;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout)) < 0) {
      ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Failed to set SO_RCVTIMEO: {}",
                     GetLastErrorString());
    }
#else
    timeval tv{};
    tv.tv_sec = config_.recv_timeout_ms / 1000;
    tv.tv_usec = (config_.recv_timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      ZENREMOTE_WARN(LOG_MODULE_NETWORK, "Failed to set SO_RCVTIMEO: {}",
                     GetLastErrorString());
    }
#endif
  }

  return true;
}

std::string UdpSocket::GetLastErrorString() const {
#ifdef _WIN32
  int error = WSAGetLastError();
  char buffer[256];
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, error, 0, buffer, sizeof(buffer), nullptr);
  return std::string(buffer);
#else
  return std::string(strerror(errno));
#endif
}

}  // namespace zenremote
