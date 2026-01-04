#include "video_encoder.h"

#include "common/log_manager.h"
#include "hardware_encoder.h"
#include "software_encoder.h"

namespace zenremote {

Result<std::unique_ptr<IVideoEncoder>> CreateVideoEncoder(
    const EncoderConfig& config) {
  std::unique_ptr<IVideoEncoder> encoder;

  if (config.encoder_type == EncoderType::kHardware) {
    // 创建硬件编码器
    auto hw_encoder = std::make_unique<HardwareEncoder>();
    auto result = hw_encoder->Initialize(config);

    if (result.IsOk()) {
      return Result<std::unique_ptr<IVideoEncoder>>::Ok(std::move(hw_encoder));
    }

    // 硬件编码器初始化失败，尝试回退到软件编码器
    ZENREMOTE_WARN(
        "Hardware encoder failed: {}, falling back to software encoder",
        result.Message());

    // 创建软件编码器
    auto sw_encoder = std::make_unique<SoftwareEncoder>();
    EncoderConfig sw_config = config;
    sw_config.encoder_type = EncoderType::kSoftware;

    result = sw_encoder->Initialize(sw_config);
    if (result.IsErr()) {
      return Result<std::unique_ptr<IVideoEncoder>>::Err(
          result.Code(),
          fmt::format("Software encoder fallback also failed: {}",
                      result.Message()));
    }

    return Result<std::unique_ptr<IVideoEncoder>>::Ok(std::move(sw_encoder));

  } else {
    // 创建软件编码器
    auto sw_encoder = std::make_unique<SoftwareEncoder>();
    auto result = sw_encoder->Initialize(config);

    if (result.IsErr()) {
      return Result<std::unique_ptr<IVideoEncoder>>::Err(result.Code(),
                                                         result.Message());
    }

    return Result<std::unique_ptr<IVideoEncoder>>::Ok(std::move(sw_encoder));
  }
}

}  // namespace zenremote
