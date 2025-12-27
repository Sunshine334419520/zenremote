#pragma once

#include <cstdint>
#include <memory>

#include "common/error.h"

namespace zenremote {

class BaseConnection;

/**
 * @brief 可靠传输层
 */
class ReliableTransport {
 public:
  struct Config {
    int max_retries = 3;
    bool ordered = true;
    int timeout_ms = 100;
  };

  ReliableTransport(BaseConnection* connection, const Config& config);
  ~ReliableTransport();

  Result<void> Send(const uint8_t* data, size_t length);
  void OnAckReceived(uint32_t sequence_number);

 private:
  BaseConnection* connection_;
  Config config_;
  uint32_t sequence_number_ = 0;
};

}  // namespace zenremote
