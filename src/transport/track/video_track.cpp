#include "video_track.h"

#include <random>

#include "network/connection/base_connection.h"
#include "network/rtp/rtp_sender.h"

namespace zenremote {

VideoTrack::VideoTrack(const Config& config) : config_(config) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist(1000, 999999);
  ssrc_ = dist(gen);
}

VideoTrack::~VideoTrack() = default;

Result<void> VideoTrack::SendFrame(const uint8_t* data,
                                   size_t length,
                                   uint32_t timestamp_90khz) {
  if (!enabled_) {
    return Result<void>::Err(ErrorCode::kInvalidOperation, "Track is disabled");
  }

  if (!rtp_sender_) {
    return Result<void>::Err(ErrorCode::kNotInitialized, "Track not connected");
  }

  return rtp_sender_->SendVideoFrame(data, length, timestamp_90khz, true);
}

void VideoTrack::SetConnection(BaseConnection* connection) {
  if (!connection) {
    rtp_sender_.reset();
    return;
  }

  RTPSender::Config rtp_config;
  rtp_config.ssrc = ssrc_;
  rtp_config.payload_type = 96;
  rtp_config.clock_rate = config_.clock_rate;

  rtp_sender_ = std::make_unique<RTPSender>(connection, rtp_config);
}

}  // namespace zenremote
