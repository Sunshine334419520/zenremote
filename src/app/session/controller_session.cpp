#include "controller_session.h"

#include "common/log_manager.h"

namespace zenremote {

ControllerSession::ControllerSession() = default;

ControllerSession::~ControllerSession() {
  Shutdown();
}

Result<void> ControllerSession::Initialize(const Config& config) {
  config_ = config;

  peer_connection_ = std::make_unique<PeerConnection>();

  PeerConnection::Config pc_config;
  pc_config.mode = PeerConnection::ConnectionMode::kDirect;
  pc_config.remote_ip = config_.remote_ip;
  pc_config.remote_port = config_.remote_port;

  auto result = peer_connection_->Initialize(pc_config);
  if (result.IsErr()) {
    return Result<void>::Err(
        ErrorCode::kPeerConnectionError,
        "Failed to initialize PeerConnection: " + result.Message());
  }

  if (config_.enable_video) {
    VideoTrack::Config video_config;
    video_config.id = "video0";
    video_config.codec = "H264";
    video_config.bitrate_bps = config_.video_bitrate_bps;
    video_config.framerate = config_.video_framerate;

    video_track_ = std::make_shared<VideoTrack>(video_config);
    auto add_result = peer_connection_->AddTrack(video_track_);
    if (add_result.IsErr()) {
      return Result<void>::Err(
          ErrorCode::kVideoTrackError,
          "Failed to add video track: " + add_result.Message());
    }
  }

  if (config_.enable_audio) {
    AudioTrack::Config audio_config;
    audio_config.id = "audio0";
    audio_config.codec = "Opus";
    audio_config.sample_rate = config_.audio_sample_rate;

    audio_track_ = std::make_shared<AudioTrack>(audio_config);
    auto add_result = peer_connection_->AddTrack(audio_track_);
    if (add_result.IsErr()) {
      return Result<void>::Err(
          ErrorCode::kAudioTrackError,
          "Failed to add audio track: " + add_result.Message());
    }
  }

  DataChannel::Config channel_config;
  channel_config.ordered = true;
  channel_config.max_retransmits = 3;

  auto channel_result =
      peer_connection_->CreateDataChannel("input", channel_config);
  if (channel_result.IsOk()) {
    input_channel_ = channel_result.Value();
    input_channel_->SetOnMessageCallback(
        [this](const uint8_t* data, size_t len) {
          OnInputEventReceived(data, len);
        });
  } else {
    ZENREMOTE_WARN("Failed to create input channel: {}",
                   channel_result.Message());
  }

  result = peer_connection_->Connect();
  if (result.IsErr()) {
    return Result<void>::Err(ErrorCode::kPeerConnectionError,
                             "Failed to connect: " + result.Message());
  }

  ZENREMOTE_INFO("ControllerSession initialized and connected");
  return Result<void>::Ok();
}

void ControllerSession::Shutdown() {
  if (peer_connection_) {
    peer_connection_->Disconnect();
    peer_connection_.reset();
  }

  video_track_.reset();
  audio_track_.reset();
  input_channel_.reset();

  ZENREMOTE_INFO("ControllerSession shut down");
}

Result<void> ControllerSession::SendVideoFrame(const uint8_t* data,
                                               size_t length,
                                               uint32_t timestamp_90khz) {
  if (!video_track_) {
    return Result<void>::Err(ErrorCode::kVideoTrackError,
                             "Video track not initialized");
  }

  return video_track_->SendFrame(data, length, timestamp_90khz);
}

Result<void> ControllerSession::SendAudioPacket(const uint8_t* data,
                                                size_t length,
                                                uint32_t timestamp_48khz) {
  if (!audio_track_) {
    return Result<void>::Err(ErrorCode::kAudioTrackError,
                             "Audio track not initialized");
  }

  return audio_track_->SendFrame(data, length, timestamp_48khz);
}

void ControllerSession::SetVideoEnabled(bool enabled) {
  if (video_track_) {
    video_track_->SetEnabled(enabled);
  }
}

void ControllerSession::SetAudioEnabled(bool enabled) {
  if (audio_track_) {
    audio_track_->SetEnabled(enabled);
  }
}

void ControllerSession::OnInputEventReceived(const uint8_t* data,
                                             size_t length) {
  ZENREMOTE_DEBUG("Received input event, {} bytes", length);
}

}  // namespace zenremote
