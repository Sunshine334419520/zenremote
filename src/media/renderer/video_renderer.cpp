#include "video_renderer.h"

#include "sdl_renderer.h"

#ifdef _WIN32
#include "d3d11_renderer.h"
#endif

#include "common/log_manager.h"

namespace zenremote {

Result<std::unique_ptr<IVideoRenderer>> CreateVideoRenderer(RendererType type) {
  std::unique_ptr<IVideoRenderer> renderer;

  switch (type) {
    case RendererType::kSDL:
      renderer = std::make_unique<SDLRenderer>();
      break;

#ifdef _WIN32
    case RendererType::kD3D11:
      renderer = std::make_unique<D3D11Renderer>();
      break;
#endif

    case RendererType::kOpenGL:
      return Result<std::unique_ptr<IVideoRenderer>>::Err(
          ErrorCode::kNotImplemented, "OpenGL renderer not implemented yet");

    default:
      return Result<std::unique_ptr<IVideoRenderer>>::Err(
          ErrorCode::kInvalidParameter, "Unknown renderer type");
  }

  if (!renderer) {
    return Result<std::unique_ptr<IVideoRenderer>>::Err(
        ErrorCode::kOutOfMemory, "Failed to create renderer");
  }

  ZENREMOTE_DEBUG("Created {} renderer", RendererTypeToString(type));
  return Result<std::unique_ptr<IVideoRenderer>>::Ok(std::move(renderer));
}

}  // namespace zenremote
