#pragma once

#include <chrono>
#include <cstdint>

namespace zenremote {

class Pacer {
 public:
  struct Config {
    uint32_t pacing_interval_ms = 5;
    uint32_t max_packets_per_batch = 10;
  };

  explicit Pacer(const Config& config);

  bool CanSend();

  void OnPacketSent();

  void Reset();

 private:
  Config config_;
  std::chrono::steady_clock::time_point last_send_time_;
  int packets_in_batch_ = 0;
};

}  // namespace zenremote
