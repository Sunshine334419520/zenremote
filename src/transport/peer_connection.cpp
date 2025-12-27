#include "peer_connection.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#include "channel/reliable_channel.h"
#include "network/connection/base_connection.h"
#include "network/connection/direct_connection.h"

namespace zenremote {

PeerConnection::PeerConnection() = default;

PeerConnection::~PeerConnection() {
  Disconnect();
}

Result<void> PeerConnection::Initialize(const Config& config) {
  config_ = config;

  switch (config_.mode) {
    case ConnectionMode::kDirect: {
      auto direct_conn = std::make_unique<DirectConnection>();
      DirectConnection::Config conn_config;
      conn_config.remote.address = config_.remote_ip;
      conn_config.remote.port = config_.remote_port;
      conn_config.local_port = config_.local_port;

      auto result = direct_conn->Initialize(conn_config);
      if (result.IsErr()) {
        return Result<void>::Err(
            result.Code(),
            "Failed to initialize direct connection: " + result.Message());
      }

      connection_ = std::move(direct_conn);
      break;
    }

    case ConnectionMode::kRelay:
      return Result<void>::Err(ErrorCode::kNotImplemented,
                               "TURN relay not implemented yet");

    case ConnectionMode::kAuto:
      return Result<void>::Err(ErrorCode::kNotImplemented,
                               "Auto mode not implemented yet");

    default:
      return Result<void>::Err(ErrorCode::kInvalidParameter,
                               "Invalid connection mode");
  }

  return Result<void>::Ok();
}

Result<void> PeerConnection::Connect() {
  if (!connection_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "PeerConnection not initialized");
  }

  auto result = connection_->Open();
  if (result.IsErr()) {
    return Result<void>::Err(result.Code(),
                             "Failed to open connection: " + result.Message());
  }

  for (auto& track : tracks_) {
    track->SetConnection(connection_.get());
  }

  for (auto& channel : data_channels_) {
    channel->SetConnection(connection_.get());
  }

  should_stop_ = false;
  receive_thread_ = std::make_unique<std::thread>([this]() { ReceiveLoop(); });

  spdlog::info("PeerConnection connected");
  return Result<void>::Ok();
}

void PeerConnection::Disconnect() {
  if (!connection_) {
    return;
  }

  should_stop_ = true;
  if (receive_thread_ && receive_thread_->joinable()) {
    receive_thread_->join();
  }

  for (auto& track : tracks_) {
    track->SetConnection(nullptr);
  }

  for (auto& channel : data_channels_) {
    channel->SetConnection(nullptr);
  }

  connection_->Close();
  connection_.reset();

  spdlog::info("PeerConnection disconnected");
}

bool PeerConnection::IsConnected() const {
  return connection_ && connection_->IsOpen();
}

Result<void> PeerConnection::AddTrack(std::shared_ptr<MediaTrack> track) {
  if (!track) {
    return Result<void>::Err(ErrorCode::kInvalidParameter, "Track is null");
  }

  auto it = std::find_if(tracks_.begin(), tracks_.end(), [&](const auto& t) {
    return t->GetId() == track->GetId();
  });
  if (it != tracks_.end()) {
    return Result<void>::Err(ErrorCode::kInvalidOperation,
                             "Track already exists: " + track->GetId());
  }

  tracks_.push_back(track);

  if (IsConnected()) {
    track->SetConnection(connection_.get());
  }

  spdlog::info("Added track: {}", track->GetId());
  return Result<void>::Ok();
}

Result<void> PeerConnection::RemoveTrack(const std::string& track_id) {
  auto it = std::find_if(tracks_.begin(), tracks_.end(),
                         [&](const auto& t) { return t->GetId() == track_id; });

  if (it == tracks_.end()) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Track not found: " + track_id);
  }

  (*it)->SetConnection(nullptr);
  tracks_.erase(it);

  spdlog::info("Removed track: {}", track_id);
  return Result<void>::Ok();
}

std::vector<std::shared_ptr<MediaTrack>> PeerConnection::GetTracks() const {
  return tracks_;
}

std::shared_ptr<MediaTrack> PeerConnection::GetTrack(
    const std::string& track_id) const {
  auto it = std::find_if(tracks_.begin(), tracks_.end(),
                         [&](const auto& t) { return t->GetId() == track_id; });
  return it != tracks_.end() ? *it : nullptr;
}

Result<std::shared_ptr<DataChannel>> PeerConnection::CreateDataChannel(
    const std::string& label,
    const DataChannel::Config& config) {
  auto existing = GetDataChannel(label);
  if (existing) {
    return Result<std::shared_ptr<DataChannel>>::Err(
        ErrorCode::kInvalidOperation, "DataChannel already exists: " + label);
  }

  auto channel = std::make_shared<ReliableChannel>(label, config);
  data_channels_.push_back(channel);

  if (IsConnected()) {
    channel->SetConnection(connection_.get());
  }

  spdlog::info("Created DataChannel: {}", label);
  return Result<std::shared_ptr<DataChannel>>::Ok(channel);
}

std::shared_ptr<DataChannel> PeerConnection::GetDataChannel(
    const std::string& label) const {
  auto it =
      std::find_if(data_channels_.begin(), data_channels_.end(),
                   [&](const auto& ch) { return ch->GetLabel() == label; });
  return it != data_channels_.end() ? *it : nullptr;
}

uint32_t PeerConnection::AllocateSSRC() {
  return next_ssrc_++;
}

void PeerConnection::ReceiveLoop() {
  std::vector<uint8_t> buffer(65536);

  while (!should_stop_) {
    auto result = connection_->Recv(buffer.data(), buffer.size(), 100);

    if (result.IsOk()) {
      size_t received = result.Value();
      if (received > 0) {
        ProcessReceivedPacket(buffer.data(), received);
      }
    }
  }
}

void PeerConnection::ProcessReceivedPacket(const uint8_t* data, size_t length) {
  spdlog::debug("Received {} bytes", length);
}

}  // namespace zenremote
