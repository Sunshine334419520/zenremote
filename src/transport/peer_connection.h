#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "channel/data_channel.h"
#include "common/error.h"
#include "network/connection/base_connection.h"
#include "track/audio_track.h"
#include "track/media_track.h"
#include "track/video_track.h"

namespace zenremote {

/**
 * @brief 对等连接
 *
 * 参考 WebRTC RTCPeerConnection
 * 管理连接和所有传输通道
 */
class PeerConnection {
 public:
  enum class ConnectionMode {
    kDirect,
    kRelay,
    kAuto,
  };

  struct Config {
    ConnectionMode mode = ConnectionMode::kDirect;
    std::string remote_ip;
    uint16_t remote_port = 0;
    uint16_t local_port = 0;

    std::string turn_server;
    std::string turn_username;
    std::string turn_password;
  };

  PeerConnection();
  ~PeerConnection();

  Result<void> Initialize(const Config& config);
  Result<void> Connect();
  void Disconnect();
  bool IsConnected() const;

  Result<void> AddTrack(std::shared_ptr<MediaTrack> track);
  Result<void> RemoveTrack(const std::string& track_id);
  std::vector<std::shared_ptr<MediaTrack>> GetTracks() const;
  std::shared_ptr<MediaTrack> GetTrack(const std::string& track_id) const;

  Result<std::shared_ptr<DataChannel>> CreateDataChannel(
      const std::string& label,
      const DataChannel::Config& config = {});
  std::shared_ptr<DataChannel> GetDataChannel(const std::string& label) const;

  using OnTrackCallback = std::function<void(std::shared_ptr<MediaTrack>)>;
  using OnDataChannelCallback =
      std::function<void(std::shared_ptr<DataChannel>)>;

  void SetOnTrackCallback(OnTrackCallback callback) {
    on_track_callback_ = std::move(callback);
  }

  void SetOnDataChannelCallback(OnDataChannelCallback callback) {
    on_datachannel_callback_ = std::move(callback);
  }

 private:
  uint32_t AllocateSSRC();
  void ReceiveLoop();
  void ProcessReceivedPacket(const uint8_t* data, size_t length);

  Config config_;
  std::unique_ptr<BaseConnection> connection_;

  std::vector<std::shared_ptr<MediaTrack>> tracks_;
  std::vector<std::shared_ptr<DataChannel>> data_channels_;

  OnTrackCallback on_track_callback_;
  OnDataChannelCallback on_datachannel_callback_;

  uint32_t next_ssrc_ = 1000;

  std::unique_ptr<std::thread> receive_thread_;
  std::atomic<bool> should_stop_{false};
};

}  // namespace zenremote
