#include "network/protocol/pacer.h"

namespace zenremote {

Pacer::Pacer(const Config& config)
    : config_(config), last_send_time_(std::chrono::steady_clock::now()) {}

bool Pacer::CanSend() {
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_send_time_)
                        .count();

  if (elapsed_ms >= config_.pacing_interval_ms) {
    packets_in_batch_ = 0;
    return true;
  }

  if (packets_in_batch_ < static_cast<int>(config_.max_packets_per_batch)) {
    return true;
  }

  return false;
}

void Pacer::OnPacketSent() {
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_send_time_)
                        .count();

  if (elapsed_ms >= config_.pacing_interval_ms) {
    last_send_time_ = now;
    packets_in_batch_ = 1;
  } else {
    packets_in_batch_++;
  }
}

void Pacer::Reset() {
  last_send_time_ = std::chrono::steady_clock::now();
  packets_in_batch_ = 0;
}

}  // namespace zenremote
