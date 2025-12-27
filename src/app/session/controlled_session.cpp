#include "controlled_session.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace zenremote {

#pragma pack(push, 1)
struct InputEvent {
  uint8_t type;
  union {
    struct {
      int16_t x;
      int16_t y;
    } mouse_move;
    struct {
      uint8_t button;
      uint8_t is_down;
      int16_t x;
      int16_t y;
    } mouse_click;
    struct {
      uint32_t key_code;
      uint8_t is_down;
      uint32_t modifiers;
    } key_event;
  };
};
#pragma pack(pop)

ControlledSession::ControlledSession() = default;

ControlledSession::~ControlledSession() {
  Shutdown();
}

Result<void> ControlledSession::Initialize(const Config& config) {
  config_ = config;

  peer_connection_ = std::make_unique<PeerConnection>();

  PeerConnection::Config pc_config;
  pc_config.mode = PeerConnection::ConnectionMode::kDirect;
  pc_config.local_port = config_.local_port;

  auto result = peer_connection_->Initialize(pc_config);
  if (result.IsErr()) {
    return Result<void>::Err(
        ErrorCode::kNotInitialized,
        "Failed to initialize PeerConnection: " + result.Message());
  }

  peer_connection_->SetOnTrackCallback(
      [this](std::shared_ptr<MediaTrack> track) { OnRemoteTrackAdded(track); });

  peer_connection_->SetOnDataChannelCallback(
      [this](std::shared_ptr<DataChannel> channel) {
        OnRemoteDataChannel(channel);
      });

  result = peer_connection_->Connect();
  if (result.IsErr()) {
    return Result<void>::Err(ErrorCode::kConnectionFailed,
                             "Failed to connect: " + result.Message());
  }

  spdlog::info("ControlledSession initialized and waiting for connection");
  return Result<void>::Ok();
}

void ControlledSession::Shutdown() {
  if (peer_connection_) {
    peer_connection_->Disconnect();
    peer_connection_.reset();
  }

  input_channel_.reset();

  spdlog::info("ControlledSession shut down");
}

Result<void> ControlledSession::SendMouseMove(int x, int y) {
  if (!input_channel_ ||
      input_channel_->GetState() != DataChannel::State::kOpen) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Input channel not ready");
  }

  InputEvent event;
  event.type = 0;
  event.mouse_move.x = static_cast<int16_t>(x);
  event.mouse_move.y = static_cast<int16_t>(y);

  return input_channel_->Send(reinterpret_cast<const uint8_t*>(&event),
                              sizeof(event));
}

Result<void> ControlledSession::SendMouseClick(int button,
                                               bool is_down,
                                               int x,
                                               int y) {
  if (!input_channel_ ||
      input_channel_->GetState() != DataChannel::State::kOpen) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Input channel not ready");
  }

  InputEvent event;
  event.type = 1;
  event.mouse_click.button = static_cast<uint8_t>(button);
  event.mouse_click.is_down = is_down ? 1 : 0;
  event.mouse_click.x = static_cast<int16_t>(x);
  event.mouse_click.y = static_cast<int16_t>(y);

  return input_channel_->Send(reinterpret_cast<const uint8_t*>(&event),
                              sizeof(event));
}

Result<void> ControlledSession::SendKeyEvent(uint32_t key_code,
                                             bool is_down,
                                             uint32_t modifiers) {
  if (!input_channel_ ||
      input_channel_->GetState() != DataChannel::State::kOpen) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Input channel not ready");
  }

  InputEvent event;
  event.type = 2;
  event.key_event.key_code = key_code;
  event.key_event.is_down = is_down ? 1 : 0;
  event.key_event.modifiers = modifiers;

  return input_channel_->Send(reinterpret_cast<const uint8_t*>(&event),
                              sizeof(event));
}

void ControlledSession::OnRemoteTrackAdded(std::shared_ptr<MediaTrack> track) {
  spdlog::info(
      "Remote track added: {}, kind: {}", track->GetId(),
      track->GetKind() == MediaTrack::Kind::kVideo ? "video" : "audio");

  if (track->GetKind() == MediaTrack::Kind::kVideo) {
    track->SetOnFrameCallback(
        [this](const uint8_t* data, size_t len, uint32_t ts) {
          if (on_video_frame_callback_) {
            on_video_frame_callback_(data, len, ts);
          }
        });
  } else if (track->GetKind() == MediaTrack::Kind::kAudio) {
    track->SetOnFrameCallback(
        [this](const uint8_t* data, size_t len, uint32_t ts) {
          if (on_audio_packet_callback_) {
            on_audio_packet_callback_(data, len, ts);
          }
        });
  }
}

void ControlledSession::OnRemoteDataChannel(
    std::shared_ptr<DataChannel> channel) {
  spdlog::info("Remote DataChannel: {}", channel->GetLabel());

  if (channel->GetLabel() == "input") {
    input_channel_ = channel;
    channel->SetOnOpenCallback(
        [this]() { spdlog::info("Input channel opened"); });
  }
}

}  // namespace zenremote
