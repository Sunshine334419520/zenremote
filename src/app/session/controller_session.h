#pragma once

#include <memory>
#include <string>

#include "common/error.h"
#include "transport/channel/data_channel.h"
#include "transport/track/media_track.h"
#include "transport/peer_connection.h"
#include "transport/track/video_track.h"

namespace zenremote {

/**
 * @brief 控制端会话
 *
 * 应用层类，负责被控端的屏幕捕获和音频采集
 */
class ControllerSession {
 public:
  struct Config {
    std::string remote_ip;
    uint16_t remote_port = 50000;

    bool enable_video = true;
    uint32_t video_bitrate_bps = 2500000;
    uint32_t video_framerate = 30;

    bool enable_audio = true;
    uint32_t audio_sample_rate = 48000;
  };

  ControllerSession();
  ~ControllerSession();

  Result<void> Initialize(const Config& config);
  void Shutdown();

  Result<void> SendVideoFrame(const uint8_t* data,
                              size_t length,
                              uint32_t timestamp_90khz);
  Result<void> SendAudioPacket(const uint8_t* data,
                               size_t length,
                               uint32_t timestamp_48khz);

  void SetVideoEnabled(bool enabled);
  void SetAudioEnabled(bool enabled);

 private:
  void OnInputEventReceived(const uint8_t* data, size_t length);

  Config config_;
  std::unique_ptr<PeerConnection> peer_connection_;
  std::shared_ptr<VideoTrack> video_track_;
  std::shared_ptr<AudioTrack> audio_track_;
  std::shared_ptr<DataChannel> input_channel_;
};

}  // namespace zenremote
