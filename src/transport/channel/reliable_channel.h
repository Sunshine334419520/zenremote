#pragma once

#include <memory>

#include "data_channel.h"

namespace zenremote {

// Forward declaration for BaseConnection
class BaseConnection;
class ReliableTransport;

/**
 * @brief 可靠数据通道实现
 */
class ReliableChannel : public DataChannel {
 public:
  ReliableChannel(const std::string& label, const Config& config);
  ~ReliableChannel() override;

  std::string GetLabel() const override { return label_; }
  State GetState() const override { return state_; }

  Result<void> Send(const uint8_t* data, size_t length) override;
  Result<void> Send(const std::string& text) override;

  void SetOnMessageCallback(OnMessageCallback callback) override {
    on_message_callback_ = std::move(callback);
  }

  void SetOnOpenCallback(OnOpenCallback callback) override {
    on_open_callback_ = std::move(callback);
  }

  void SetOnCloseCallback(OnCloseCallback callback) override {
    on_close_callback_ = std::move(callback);
  }

  void SetConnection(BaseConnection* connection) override;
  void OnDataReceived(const uint8_t* data, size_t length);

 private:
  std::string label_;
  Config config_;
  State state_ = State::kConnecting;

  OnMessageCallback on_message_callback_;
  OnOpenCallback on_open_callback_;
  OnCloseCallback on_close_callback_;

  std::unique_ptr<ReliableTransport> transport_;
};

}  // namespace zenremote