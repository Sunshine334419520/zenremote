#include "ffmpeg_types.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace zenremote {

void SwsContextDeleter::operator()(::SwsContext* ctx) const {
  if (ctx) {
    sws_freeContext(ctx);
  }
}

}  // namespace zenremote
