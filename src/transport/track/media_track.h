#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/error.h"
#include "network/connection/base_connection.h"

namespace zenremote {

/**
 * @brief 媒体轨道抽象
 *
 * 参考 WebRTC MediaStreamTrack
 * 每个 Track 代表一个独立的媒体流（视频或音频）
 */
class MediaTrack {
 public:
  enum class Kind {
    kVideo,
    kAudio,
  };

  virtual ~MediaTrack() = default;

  virtual std::string GetId() const = 0;
  virtual Kind GetKind() const = 0;
  virtual bool IsEnabled() const = 0;
  virtual void SetEnabled(bool enabled) = 0;

  virtual Result<void> SendFrame(const uint8_t* data,
                                 size_t length,
                                 uint32_t timestamp) = 0;

  using OnFrameCallback = std::function<
      void(const uint8_t* data, size_t length, uint32_t timestamp)>;
  virtual void SetOnFrameCallback(OnFrameCallback callback) = 0;

  virtual void SetConnection(BaseConnection* connection) = 0;
};

}  // namespace zenremote
