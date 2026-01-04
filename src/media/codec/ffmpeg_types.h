#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace zenremote {

// ======================== AVFrame 智能指针 ========================

struct AVFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};

using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

inline AVFramePtr MakeAVFrame() {
  return AVFramePtr(av_frame_alloc());
}

// ======================== AVPacket 智能指针 ========================

struct AVPacketDeleter {
  void operator()(AVPacket* pkt) const {
    if (pkt) {
      av_packet_free(&pkt);
    }
  }
};

using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

inline AVPacketPtr MakeAVPacket() {
  return AVPacketPtr(av_packet_alloc());
}

// ======================== AVCodecContext 智能指针 ========================

struct AVCodecCtxDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      AVCodecContext* tmp = ctx;
      avcodec_free_context(&tmp);
    }
  }
};

using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecCtxDeleter>;

// ======================== SwsContext 智能指针 ========================

struct SwsContextDeleter {
  void operator()(::SwsContext* ctx) const;  // 实现在 cpp 中
};

using SwsContextPtr = std::unique_ptr<::SwsContext, SwsContextDeleter>;

// ======================== AVBufferRef 智能指针 ========================

struct AVBufferRefDeleter {
  void operator()(AVBufferRef* ref) const {
    if (ref) {
      av_buffer_unref(&ref);
    }
  }
};

using AVBufferRefPtr = std::unique_ptr<AVBufferRef, AVBufferRefDeleter>;

}  // namespace zenremote
