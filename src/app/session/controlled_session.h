#pragma once

#include <memory>
#include <string>

#include "common/error.h"
#include "transport/channel/data_channel.h"
#include "transport/track/media_track.h"
#include "transport/peer_connection.h"

namespace zenremote {

/**
 * @brief 被控端会话
 *
 * 应用层类，接收屏幕和音频进行渲染，发送本地输入事件
 */
class ControlledSession {
 public:
  struct Config {
    uint16_t local_port = 50000;
  };

  ControlledSession();
  ~ControlledSession();

  Result<void> Initialize(const Config& config);
  void Shutdown();

  Result<void> SendMouseMove(int x, int y);
  Result<void> SendMouseClick(int button, bool is_down, int x, int y);
  Result<void> SendKeyEvent(uint32_t key_code,
                            bool is_down,
                            uint32_t modifiers);

  using OnVideoFrameCallback =
      std::function<void(const uint8_t*, size_t, uint32_t)>;
  using OnAudioPacketCallback =
      std::function<void(const uint8_t*, size_t, uint32_t)>;

  void SetOnVideoFrameCallback(OnVideoFrameCallback callback) {
    on_video_frame_callback_ = std::move(callback);
  }

  void SetOnAudioPacketCallback(OnAudioPacketCallback callback) {
    on_audio_packet_callback_ = std::move(callback);
  }

 private:
  void OnRemoteTrackAdded(std::shared_ptr<MediaTrack> track);
  void OnRemoteDataChannel(std::shared_ptr<DataChannel> channel);

  Config config_;
  std::unique_ptr<PeerConnection> peer_connection_;
  std::shared_ptr<DataChannel> input_channel_;

  OnVideoFrameCallback on_video_frame_callback_;
  OnAudioPacketCallback on_audio_packet_callback_;
};

}  // namespace zenremote
