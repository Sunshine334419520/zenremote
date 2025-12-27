#pragma once

#include "media_track.h"
#include "network/rtp/rtp_sender.h"

namespace zenremote {

/**
 * @brief 音频轨道
 */
class AudioTrack : public MediaTrack {
 public:
  struct Config {
    std::string id = "audio";
    std::string codec = "Opus";
    uint32_t sample_rate = 48000;
    uint32_t clock_rate = 48000;
    uint32_t channels = 2;
  };

  explicit AudioTrack(const Config& config);
  ~AudioTrack() override;

  std::string GetId() const override { return config_.id; }
  Kind GetKind() const override { return Kind::kAudio; }
  bool IsEnabled() const override { return enabled_; }
  void SetEnabled(bool enabled) override { enabled_ = enabled; }

  Result<void> SendFrame(const uint8_t* data,
                         size_t length,
                         uint32_t timestamp_48khz) override;

  void SetOnFrameCallback(OnFrameCallback callback) override {
    on_frame_callback_ = std::move(callback);
  }

  void SetConnection(BaseConnection* connection) override;

  const Config& GetConfig() const { return config_; }
  uint32_t GetSSRC() const { return ssrc_; }

 private:
  Config config_;
  bool enabled_ = true;
  uint32_t ssrc_ = 0;
  OnFrameCallback on_frame_callback_;

  std::unique_ptr<RTPSender> rtp_sender_;
};

}  // namespace zenremote