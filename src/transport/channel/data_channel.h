#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "common/error.h"
#include "network/connection/base_connection.h"

namespace zenremote {

/**
 * @brief 数据通道抽象
 *
 * 参考 WebRTC RTCDataChannel
 */
class DataChannel {
 public:
  struct Config {
    bool ordered = true;
    int max_retransmits = 3;
    int max_packet_life_time = 0;
  };

  enum class State {
    kConnecting,
    kOpen,
    kClosing,
    kClosed,
  };

  virtual ~DataChannel() = default;

  virtual std::string GetLabel() const = 0;
  virtual State GetState() const = 0;

  virtual Result<void> Send(const uint8_t* data, size_t length) = 0;
  virtual Result<void> Send(const std::string& text) = 0;

  using OnMessageCallback =
      std::function<void(const uint8_t* data, size_t length)>;
  virtual void SetOnMessageCallback(OnMessageCallback callback) = 0;

  using OnOpenCallback = std::function<void()>;
  using OnCloseCallback = std::function<void()>;
  virtual void SetOnOpenCallback(OnOpenCallback callback) = 0;
  virtual void SetOnCloseCallback(OnCloseCallback callback) = 0;

  virtual void SetConnection(BaseConnection* connection) = 0;
};

}  // namespace zenremote
