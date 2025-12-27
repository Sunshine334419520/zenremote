#pragma once

#include "media_track.h"
#include "network/rtp/rtp_sender.h"

namespace zenremote {

/**
 * @brief 视频轨道
 */
class VideoTrack : public MediaTrack {
 public:
  struct Config {
    std::string id = "video";
    std::string codec = "H264";
    uint32_t bitrate_bps = 2500000;
    uint32_t framerate = 30;
    uint32_t clock_rate = 90000;
  };

  explicit VideoTrack(const Config& config);
  ~VideoTrack() override;

  std::string GetId() const override { return config_.id; }
  Kind GetKind() const override { return Kind::kVideo; }
  bool IsEnabled() const override { return enabled_; }
  void SetEnabled(bool enabled) override { enabled_ = enabled; }

  Result<void> SendFrame(const uint8_t* data,
                         size_t length,
                         uint32_t timestamp_90khz) override;

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
