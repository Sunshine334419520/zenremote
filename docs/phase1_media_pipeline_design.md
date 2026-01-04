# Phase 1: 核心媒体管线设计文档

## 文档信息

| 项目 | 内容 |
|------|------|
| **版本** | v1.0 |
| **创建日期** | 2025-01-04 |
| **作者** | GitHub Copilot |
| **状态** | 设计阶段 |
| **目标平台** | Windows (首要)，后续支持 macOS |

---

## 1. 设计目标与范围

### 1.1 Phase 1 目标

Phase 1 聚焦于建立完整的 **视频媒体管线**，实现从屏幕采集到远程显示的端到端流程：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Phase 1 范围                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  被控端 (Controlled)                      控制端 (Controller)                 │
│  ┌─────────────────────┐                 ┌─────────────────────┐            │
│  │   Screen Capturer   │                 │    Video Decoder    │            │
│  │   (已完成 ✅)        │                 │    (待实现 ❌)       │            │
│  └──────────┬──────────┘                 └──────────▲──────────┘            │
│             │ BGRA Frame                            │ AVPacket              │
│             ▼                                       │                       │
│  ┌─────────────────────┐                 ┌─────────┴──────────┐             │
│  │   Video Encoder     │   ─────────>    │   Network Layer    │             │
│  │   (待实现 ❌)        │   RTP/UDP      │   (已完成 ✅)       │             │
│  └─────────────────────┘                 └─────────┬──────────┘             │
│                                                    │ AVFrame                │
│                                                    ▼                        │
│                                          ┌─────────────────────┐            │
│                                          │   Video Renderer    │            │
│                                          │   (待实现 ❌)        │            │
│                                          └─────────────────────┘            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 设计原则

| 原则 | 说明 |
|------|------|
| **低延迟优先** | 远程桌面对延迟敏感，所有设计决策以最小化延迟为首要目标 |
| **硬件加速优先** | 优先使用 GPU 硬件编解码，软件方案作为后备 |
| **优雅降级** | 硬件不可用时自动切换到软件方案，保证功能可用 |
| **零拷贝路径** | 尽可能减少 CPU-GPU 间的数据拷贝 |
| **跨平台抽象** | 接口层平台无关，实现层平台相关 |

### 1.3 与 zenplay 的关系

Phase 1 将参考并迁移 [zenplay](https://github.com/Sunshine334419520/zenplay) 项目的解码器和渲染器实现：

| zenplay 模块 | zenremote 用途 | 迁移策略 |
|-------------|---------------|---------|
| `VideoDecoder` | 接收端解码 | 直接迁移，适配命名空间和错误处理 |
| `HWDecoderContext` | 硬件解码上下文 | 直接迁移 |
| `D3D11Renderer` | Windows 硬件渲染 | 直接迁移，移除播放器特有逻辑 |
| `SDLRenderer` | 跨平台软件渲染 | 直接迁移 |

**需要新实现的模块**：
- `VideoEncoder` - 编码器（zenplay 是播放器，无编码需求）

---

## 2. 整体架构设计

### 2.1 媒体管线架构

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              媒体管线架构                                         │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│   Layer 4: Application Layer                                                     │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │  ControlledSession          │           ControllerSession               │   │
│   │  (被控端会话管理)            │           (控制端会话管理)                  │   │
│   └─────────────────────────────┴───────────────────────────────────────────┘   │
│                          │                              │                        │
│   Layer 3: Media Processing Layer                                                │
│   ┌─────────────────────────────┐  ┌────────────────────────────────────────┐   │
│   │      EncodePipeline         │  │         DecodePipeline                 │   │
│   │  ┌──────────────────────┐   │  │   ┌──────────────────────────────┐    │   │
│   │  │  ScreenCapturer      │   │  │   │     VideoDecoder             │    │   │
│   │  │  (DXGI/SCKit)        │   │  │   │   (D3D11VA/VT/Software)      │    │   │
│   │  └──────────┬───────────┘   │  │   └──────────────┬───────────────┘    │   │
│   │             │               │  │                  │                    │   │
│   │  ┌──────────▼───────────┐   │  │   ┌──────────────▼───────────────┐    │   │
│   │  │   ColorConverter     │   │  │   │       VideoRenderer          │    │   │
│   │  │   (BGRA → NV12)      │   │  │   │   (D3D11/SDL/Metal)          │    │   │
│   │  └──────────┬───────────┘   │  │   └──────────────────────────────┘    │   │
│   │             │               │  │                                       │   │
│   │  ┌──────────▼───────────┐   │  │                                       │   │
│   │  │    VideoEncoder      │   │  │                                       │   │
│   │  │ (NVENC/QSV/AMF/x264) │   │  │                                       │   │
│   │  └──────────────────────┘   │  │                                       │   │
│   └─────────────────────────────┘  └────────────────────────────────────────┘   │
│                          │                              ▲                        │
│   Layer 2: Transport Layer (已完成)                                              │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │         VideoTrack (RTP)        │        VideoTrack (RTP)               │   │
│   │              │                  │              ▲                        │   │
│   │              ▼                  │              │                        │   │
│   │         Pacer / JitterBuffer    │         JitterBuffer                  │   │
│   └─────────────────────────────────┴───────────────────────────────────────┘   │
│                                                                                  │
│   Layer 1: Network Layer (已完成)                                                │
│   ┌─────────────────────────────────────────────────────────────────────────┐   │
│   │                    UDP Socket / RTP Sender / RTP Receiver               │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 数据流向

#### 2.2.1 编码端数据流（被控端）

```
Screen → DXGI Capture → BGRA Frame → sws_scale → NV12/YUV420P → Encoder → AVPacket → RTP
                           │                                        │
                           │ GPU Memory (D3D11 Texture)             │ GPU Memory (if HW)
                           │                                        │
                           ▼                                        ▼
                    ~4-8ms 采集延迟                            ~2-5ms 编码延迟
```

#### 2.2.2 解码端数据流（控制端）

```
RTP → JitterBuffer → AVPacket → Decoder → AVFrame → Renderer → Display
                                    │                    │
                                    │ GPU Memory         │ GPU Memory (if D3D11)
                                    │ (if HW Decode)     │
                                    ▼                    ▼
                              ~2-5ms 解码延迟      ~1-2ms 渲染延迟
```

---

## 3. 视频编码器设计

### 3.1 编码器类型选择策略

```
                        ┌─────────────────────────────────────┐
                        │     Encoder Selection Strategy      │
                        └─────────────────────────────────────┘
                                         │
                           检测可用硬件编码器
                                         │
        ┌────────────────────────────────┼────────────────────────────────┐
        │                                │                                │
        ▼                                ▼                                ▼
  ┌───────────┐                   ┌───────────┐                   ┌───────────┐
  │  NVENC    │                   │    QSV    │                   │    AMF    │
  │ (NVIDIA)  │                   │  (Intel)  │                   │   (AMD)   │
  │ 优先级:100│                   │ 优先级:90 │                   │ 优先级:80 │
  └─────┬─────┘                   └─────┬─────┘                   └─────┬─────┘
        │                                │                                │
        └────────────────────────────────┼────────────────────────────────┘
                                         │
                                  硬件全部不可用?
                                         │
                                         ▼
                                  ┌───────────┐
                                  │  libx264  │
                                  │ (Software)│
                                  │ 优先级:0  │
                                  └───────────┘
```

### 3.2 编码器类结构

```cpp
// ======================== 编码器配置 ========================

struct EncoderConfig {
    // 基础参数
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate_bps = 2000000;  // 2 Mbps
    
    // 编码器选择
    bool prefer_hw_accel = true;        // 优先硬件加速
    std::string preferred_encoder;       // 指定编码器 (可选)
    
    // 低延迟配置
    std::string preset = "ultrafast";    // libx264 preset
    std::string tune = "zerolatency";    // libx264 tune
    int gop_size = 60;                   // I帧间隔 (2秒@30fps)
    int max_b_frames = 0;                // 禁用B帧 (降低延迟)
    
    // 码率控制
    enum class RateControlMode {
        CBR,    // 恒定码率
        VBR,    // 可变码率
        CRF     // 恒定质量
    };
    RateControlMode rc_mode = RateControlMode::CBR;
    int crf = 23;  // CRF 模式质量参数
};

// ======================== 编码器接口 ========================

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    
    // 生命周期
    virtual Result<void> Open(const EncoderConfig& config) = 0;
    virtual void Close() = 0;
    virtual bool IsOpened() const = 0;
    
    // 编码操作
    virtual Result<std::vector<AVPacketPtr>> Encode(AVFrame* frame) = 0;
    virtual Result<std::vector<AVPacketPtr>> Flush() = 0;
    
    // 状态查询
    virtual bool IsHardwareEncoder() const = 0;
    virtual const char* GetEncoderName() const = 0;
    virtual EncoderType GetEncoderType() const = 0;
};

// ======================== 编码器类型枚举 ========================

enum class EncoderType {
    kNone = 0,
    
    // 硬件编码器 (Windows)
    kNVENC,       // NVIDIA NVENC (h264_nvenc)
    kQSV,         // Intel Quick Sync (h264_qsv)
    kAMF,         // AMD AMF (h264_amf)
    
    // 硬件编码器 (macOS) - Phase 5
    kVideoToolbox,  // Apple VideoToolbox
    
    // 软件编码器
    kLibx264,     // libx264 (软件后备)
};
```

### 3.3 色彩空间转换设计

屏幕采集输出 BGRA 格式，而编码器需要 NV12/YUV420P：

```
┌─────────────────────────────────────────────────────────────────┐
│                     Color Conversion Pipeline                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   DXGI Capture Output          Encoder Input                    │
│   ┌───────────────┐           ┌───────────────┐                 │
│   │     BGRA      │           │    NV12       │  ← NVENC/QSV    │
│   │   (32-bit)    │  ──────>  │   (12-bit)    │                 │
│   │  GPU Texture  │  sws_scale│  GPU Texture  │                 │
│   └───────────────┘           └───────────────┘                 │
│                                      or                          │
│                               ┌───────────────┐                 │
│                               │   YUV420P     │  ← libx264      │
│                               │   (12-bit)    │                 │
│                               │  CPU Memory   │                 │
│                               └───────────────┘                 │
│                                                                  │
│   转换方式:                                                      │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ 硬件路径 (推荐): D3D11 Video Processor (GPU 上完成)      │   │
│   │ 软件路径 (后备): FFmpeg swscale (CPU 上完成)             │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.4 编码器低延迟配置

```cpp
// 关键低延迟配置参数

// 1. 禁用 B 帧 (最重要)
codec_ctx->max_b_frames = 0;

// 2. 低延迟标志
codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

// 3. GOP 大小 (I帧间隔)
codec_ctx->gop_size = fps * 2;  // 每2秒一个I帧

// 4. libx264 特定配置
av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);

// 5. NVENC 特定配置
av_opt_set(codec_ctx->priv_data, "preset", "p1", 0);      // 最快preset
av_opt_set(codec_ctx->priv_data, "tune", "ll", 0);        // low latency
av_opt_set(codec_ctx->priv_data, "rc", "cbr", 0);         // 恒定码率
av_opt_set(codec_ctx->priv_data, "zerolatency", "1", 0);

// 6. QSV 特定配置
av_opt_set(codec_ctx->priv_data, "preset", "veryfast", 0);
av_opt_set(codec_ctx->priv_data, "low_power", "1", 0);
```

---

## 4. 视频解码器设计

### 4.1 解码器类结构（迁移自 zenplay）

```cpp
// ======================== 解码器基类 ========================

class Decoder {
public:
    struct DecodeStats {
        bool had_invalid_data = false;
        int send_error_code = 0;
    };
    
    virtual ~Decoder() = default;
    
    // 生命周期
    Result<void> Open(AVCodecParameters* codec_params,
                      AVDictionary** options = nullptr);
    void Close();
    bool IsOpened() const { return opened_; }
    
    // 解码操作
    bool Decode(AVPacket* packet, std::vector<AVFramePtr>* frames);
    virtual Result<AVFrame*> ReceiveFrame();
    bool Flush(std::vector<AVFramePtr>* frames);
    void FlushBuffers();
    
    // 状态查询
    const DecodeStats& last_decode_stats() const;
    AVMediaType codec_type() const;
    AVCodecContext* GetCodecContext() const;

protected:
    // 子类钩子：在 avcodec_open2 之前配置
    virtual Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) {
        return Result<void>::Ok();
    }
    
    std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
    AVFramePtr work_frame_ = nullptr;
    AVMediaType codec_type_ = AVMEDIA_TYPE_UNKNOWN;
    bool opened_ = false;
    DecodeStats last_decode_stats_{};
};

// ======================== 视频解码器 ========================

class VideoDecoder : public Decoder {
public:
    Result<void> Open(AVCodecParameters* codec_params,
                      AVDictionary** options = nullptr,
                      HWDecoderContext* hw_context = nullptr);
    
    // 硬件加速相关
    bool IsHardwareDecoding() const { return hw_context_ != nullptr; }
    HWDecoderContext* GetHWContext() const { return hw_context_; }
    
    // 重写以支持零拷贝验证
    Result<AVFrame*> ReceiveFrame() override;
    
    // 视频属性访问
    int width() const;
    int height() const;
    AVPixelFormat pixel_format() const;
    AVRational time_base() const;

protected:
    Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) override;

private:
    HWDecoderContext* hw_context_ = nullptr;  // 不拥有所有权
    bool zero_copy_validated_ = false;
};
```

### 4.2 硬件解码器上下文

```cpp
// ======================== 硬件解码类型 ========================

enum class HWDecoderType {
    kNone = 0,       // 软件解码
    
    // Windows
    kD3D11VA,        // Direct3D 11 Video Acceleration (推荐)
    kDXVA2,          // DirectX Video Acceleration 2.0 (兼容)
    
    // macOS (Phase 5)
    kVideoToolbox,   // Apple VideoToolbox
    
    // Linux (未来)
    kVAAPI,          // Video Acceleration API
    kVDPAU,          // VDPAU (NVIDIA)
    
    // 跨平台
    kCUDA,           // NVIDIA CUDA
};

// ======================== 硬件解码器上下文 ========================

class HWDecoderContext {
public:
    ~HWDecoderContext();
    
    // 初始化
    Result<void> Initialize(HWDecoderType decoder_type,
                           AVCodecID codec_id,
                           int width, int height);
    
    // 配置解码器
    Result<void> ConfigureDecoder(AVCodecContext* codec_ctx);
    
    // 状态查询
    bool IsInitialized() const { return hw_device_ctx_ != nullptr; }
    HWDecoderType GetDecoderType() const { return decoder_type_; }
    
    // D3D11 特定接口 (Windows)
#ifdef _WIN32
    ID3D11Texture2D* GetD3D11Texture(AVFrame* frame);
    ID3D11Device* GetD3D11Device() const;
    ID3D11DeviceContext* GetD3D11DeviceContext() const;
#endif

    // 验证帧上下文
    bool ValidateFramesContext(AVCodecContext* codec_ctx) const;

private:
    static AVPixelFormat GetHWFormat(AVCodecContext* ctx,
                                     const AVPixelFormat* pix_fmts);
    Result<void> InitGenericHWAccel(AVCodecContext* ctx, AVPixelFormat hw_fmt);
    
    HWDecoderType decoder_type_ = HWDecoderType::kNone;
    AVBufferRef* hw_device_ctx_ = nullptr;
    AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
    
#ifdef _WIN32
    ID3D11Device* d3d11_device_ = nullptr;
    ID3D11DeviceContext* d3d11_device_context_ = nullptr;
#endif
};
```

### 4.3 解码路径选择

```
┌─────────────────────────────────────────────────────────────────┐
│                    Decoder Path Selection                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│                        检测硬件解码能力                           │
│                              │                                   │
│         ┌────────────────────┼────────────────────┐             │
│         │                    │                    │             │
│         ▼                    ▼                    ▼             │
│   ┌───────────┐       ┌───────────┐       ┌───────────┐        │
│   │  D3D11VA  │       │   DXVA2   │       │  Software │        │
│   │ Win8+推荐 │       │  Win7兼容 │       │   后备    │        │
│   └─────┬─────┘       └─────┬─────┘       └─────┬─────┘        │
│         │                   │                   │               │
│         │                   │                   │               │
│         ▼                   ▼                   ▼               │
│   ┌─────────────────────────────────────────────────────┐      │
│   │              选择最优解码器                          │      │
│   │  D3D11VA > DXVA2 > Software                         │      │
│   └─────────────────────────────────────────────────────┘      │
│                              │                                   │
│                              ▼                                   │
│                     创建 HWDecoderContext                        │
│                     配置 AVCodecContext                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. 视频渲染器设计

### 5.1 渲染器接口（迁移自 zenplay）

```cpp
// ======================== 渲染器抽象接口 ========================

class Renderer {
public:
    virtual ~Renderer() = default;
    
    // 生命周期
    virtual Result<void> Init(void* window_handle, int width, int height) = 0;
    virtual void Cleanup() = 0;
    
    // 渲染操作
    virtual bool RenderFrame(AVFrame* frame) = 0;
    virtual void Clear() = 0;
    virtual void Present() = 0;
    
    // 窗口管理
    virtual void OnResize(int width, int height) = 0;
    
    // 缓存管理 (Seek 时清理)
    virtual void ClearCaches() = 0;
    
    // 信息查询
    virtual const char* GetRendererName() const = 0;
};

// ======================== 工厂函数 ========================

std::unique_ptr<Renderer> CreateRenderer(HWDecoderType hw_decoder_type);
```

### 5.2 D3D11 渲染器结构

```cpp
// ======================== D3D11 渲染器 ========================

class D3D11Renderer : public Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer() override;
    
    // Renderer 接口实现
    Result<void> Init(void* window_handle, int width, int height) override;
    bool RenderFrame(AVFrame* frame) override;
    void Clear() override;
    void Present() override;
    void OnResize(int width, int height) override;
    void Cleanup() override;
    void ClearCaches() override;
    const char* GetRendererName() const override;
    
    // D3D11 特定接口
    void SetSharedD3D11Device(ID3D11Device* device);

private:
    Result<void> CreateShaderResourceViews(AVFrame* frame);
    Result<void> RenderQuad();
    
    // D3D11 组件
    std::unique_ptr<D3D11Context> d3d11_context_;
    std::unique_ptr<D3D11Shader> shader_;        // YUV→RGB 着色器
    std::unique_ptr<D3D11SwapChain> swap_chain_;
    
    // SRV 缓存池 (性能优化)
    struct SRVCache {
        ID3D11Texture2D* texture;
        UINT array_slice;
        ComPtr<ID3D11ShaderResourceView> y_srv;
        ComPtr<ID3D11ShaderResourceView> uv_srv;
    };
    std::vector<SRVCache> srv_pool_;
    
    // 共享设备 (来自硬件解码器)
    ID3D11Device* shared_device_ = nullptr;
    
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};
```

### 5.3 渲染路径选择

```
┌─────────────────────────────────────────────────────────────────┐
│                    Render Path Selection                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│                     检查 AVFrame 格式                            │
│                           │                                      │
│         ┌─────────────────┴─────────────────┐                   │
│         │                                   │                   │
│         ▼                                   ▼                   │
│   frame->format ==                   frame->format ==           │
│   AV_PIX_FMT_D3D11                   YUV420P / NV12             │
│         │                                   │                   │
│         ▼                                   ▼                   │
│   ┌───────────────┐                 ┌───────────────┐          │
│   │ D3D11Renderer │                 │  SDLRenderer  │          │
│   │   零拷贝      │                 │  需要上传     │          │
│   └───────┬───────┘                 └───────┬───────┘          │
│           │                                 │                   │
│           │ 直接使用硬件纹理               │ CPU→GPU 拷贝       │
│           │ ~0ms 数据传输                  │ ~2-5ms 数据传输    │
│           │                                 │                   │
│           ▼                                 ▼                   │
│   ┌─────────────────────────────────────────────────────┐      │
│   │              GPU 渲染 & Present                      │      │
│   └─────────────────────────────────────────────────────┘      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 5.4 D3D11 零拷贝渲染流程

```
┌─────────────────────────────────────────────────────────────────┐
│                  D3D11 Zero-Copy Rendering                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   硬件解码输出                                                   │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  AVFrame (format = AV_PIX_FMT_D3D11)                    │   │
│   │  ├─ data[0] = ID3D11Texture2D* (NV12 纹理)              │   │
│   │  └─ data[1] = 纹理数组索引 (array slice)                │   │
│   └─────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│   创建 Shader Resource View (SRV)                               │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  Y 平面 SRV: DXGI_FORMAT_R8_UNORM                       │   │
│   │  UV 平面 SRV: DXGI_FORMAT_R8G8_UNORM                    │   │
│   └─────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│   Pixel Shader (YUV → RGB 转换)                                 │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  // NV12 到 RGB 的 BT.709 矩阵转换                       │   │
│   │  float3 yuv = float3(y - 16/255, u - 128/255, v - 128/255);│   │
│   │  float3 rgb = mul(BT709_MATRIX, yuv);                   │   │
│   └─────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│   渲染到 SwapChain BackBuffer → Present                         │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. 文件结构规划

```
src/media/
├── codec/
│   │
│   │── encoder/                           # 编码器模块 (新实现)
│   │   ├── video_encoder.h                # 编码器接口定义
│   │   ├── video_encoder.cpp              # 编码器基础实现
│   │   ├── hw_encoder_type.h              # 硬件编码器类型定义
│   │   ├── hw_encoder_type.cpp            # 硬件编码器类型工具
│   │   └── color_converter.h/cpp          # 色彩空间转换器
│   │
│   └── decoder/                           # 解码器模块 (迁移自 zenplay)
│       ├── decoder.h                      # 解码器基类
│       ├── decoder.cpp
│       ├── video_decoder.h                # 视频解码器
│       ├── video_decoder.cpp
│       ├── hw_decoder_context.h           # 硬件解码上下文
│       ├── hw_decoder_context.cpp
│       ├── hw_decoder_type.h              # 硬件解码器类型定义
│       └── hw_decoder_type.cpp
│
├── renderer/                              # 渲染器模块 (迁移自 zenplay)
│   ├── renderer.h                         # 渲染器抽象接口
│   ├── render_path_selector.h/cpp         # 渲染路径选择器
│   │
│   ├── sdl/                               # SDL 软件渲染器
│   │   ├── sdl_renderer.h
│   │   ├── sdl_renderer.cpp
│   │   ├── sdl_manager.h                  # SDL 生命周期管理
│   │   └── sdl_manager.cpp
│   │
│   └── d3d11/                             # D3D11 硬件渲染器 (Windows)
│       ├── d3d11_renderer.h
│       ├── d3d11_renderer.cpp
│       ├── d3d11_context.h/cpp            # D3D11 设备上下文
│       ├── d3d11_shader.h/cpp             # YUV→RGB 着色器
│       └── d3d11_swap_chain.h/cpp         # 交换链管理
│
└── capture/                               # 屏幕采集模块 (已完成)
    ├── screen_capturer.h
    ├── screen_capturer_win.h
    └── screen_capturer_win.cpp
```

---

## 7. 硬件/软件设计策略

### 7.1 硬件加速优先级

| 平台 | 编码器优先级 | 解码器优先级 | 渲染器优先级 |
|------|-------------|-------------|-------------|
| **Windows** | NVENC > QSV > AMF > libx264 | D3D11VA > DXVA2 > Software | D3D11 > SDL |
| **macOS** (Phase 5) | VideoToolbox > libx264 | VideoToolbox > Software | Metal > SDL |

### 7.2 硬件检测与降级策略

```cpp
// ======================== 编码器选择逻辑 ========================

Result<std::unique_ptr<IVideoEncoder>> CreateBestEncoder(
    const EncoderConfig& config) {
    
    std::vector<EncoderType> candidates;
    
    if (config.prefer_hw_accel) {
#ifdef _WIN32
        // Windows 硬件编码器优先级
        if (IsNVENCAvailable()) {
            candidates.push_back(EncoderType::kNVENC);
        }
        if (IsQSVAvailable()) {
            candidates.push_back(EncoderType::kQSV);
        }
        if (IsAMFAvailable()) {
            candidates.push_back(EncoderType::kAMF);
        }
#endif
    }
    
    // 软件后备
    candidates.push_back(EncoderType::kLibx264);
    
    // 尝试创建编码器
    for (auto type : candidates) {
        auto encoder = CreateEncoder(type);
        auto result = encoder->Open(config);
        if (result.IsOk()) {
            LOG_INFO("Selected encoder: {}", encoder->GetEncoderName());
            return Result::Ok(std::move(encoder));
        }
        LOG_WARN("Encoder {} failed: {}", GetEncoderName(type), 
                 result.Message());
    }
    
    return Result::Err(ErrorCode::kEncoderNotFound, 
                       "No available encoder found");
}

// ======================== 解码器选择逻辑 ========================

Result<std::unique_ptr<HWDecoderContext>> CreateBestHWDecoder(
    AVCodecID codec_id, int width, int height) {
    
    auto recommended = HWDecoderTypeUtil::GetRecommendedTypes();
    
    for (auto type : recommended) {
        auto context = std::make_unique<HWDecoderContext>();
        auto result = context->Initialize(type, codec_id, width, height);
        if (result.IsOk()) {
            LOG_INFO("Selected HW decoder: {}", 
                     HWDecoderTypeUtil::GetName(type));
            return Result::Ok(std::move(context));
        }
        LOG_WARN("HW decoder {} failed: {}", 
                 HWDecoderTypeUtil::GetName(type), result.Message());
    }
    
    // 返回 nullptr 表示使用软件解码
    LOG_INFO("Using software decoder");
    return Result::Ok(nullptr);
}

// ======================== 渲染器选择逻辑 ========================

std::unique_ptr<Renderer> CreateBestRenderer(HWDecoderType hw_decoder) {
    if (hw_decoder == HWDecoderType::kD3D11VA ||
        hw_decoder == HWDecoderType::kDXVA2) {
        // 硬件解码 → D3D11 渲染器 (零拷贝)
        auto renderer = std::make_unique<D3D11Renderer>();
        return renderer;
    }
    
    // 软件解码 → SDL 渲染器
    return std::make_unique<SDLRenderer>();
}
```

### 7.3 运行时硬件检测

```cpp
// ======================== NVENC 检测 ========================

bool IsNVENCAvailable() {
    // 方法1: 尝试加载 nvEncodeAPI.dll
    HMODULE nvenc = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!nvenc) return false;
    FreeLibrary(nvenc);
    
    // 方法2: 尝试创建编码器
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    return codec != nullptr;
}

// ======================== QSV 检测 ========================

bool IsQSVAvailable() {
    // 检查 Intel Media SDK 可用性
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) return false;
    
    // 尝试创建 QSV 设备
    AVBufferRef* hw_device_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, 
                                     AV_HWDEVICE_TYPE_QSV,
                                     nullptr, nullptr, 0);
    if (ret >= 0) {
        av_buffer_unref(&hw_device_ctx);
        return true;
    }
    return false;
}

// ======================== D3D11VA 检测 ========================

bool IsD3D11VAAvailable() {
    // 尝试创建 D3D11 设备
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        feature_levels, ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION, &device, nullptr, &context);
    
    if (SUCCEEDED(hr)) {
        device->Release();
        context->Release();
        return true;
    }
    return false;
}
```

---

## 8. 性能考虑

### 8.1 延迟预算

端到端延迟目标：**< 100ms**（局域网）

| 阶段 | 目标延迟 | 优化措施 |
|------|---------|---------|
| 屏幕采集 | < 10ms | DXGI Desktop Duplication |
| 色彩转换 | < 5ms | GPU Video Processor / SIMD 优化 |
| 视频编码 | < 10ms | 硬件编码 + zerolatency |
| 网络传输 | < 30ms | UDP + RTP + Pacer |
| JitterBuffer | < 20ms | 自适应缓冲 |
| 视频解码 | < 10ms | 硬件解码 |
| 视频渲染 | < 5ms | 零拷贝渲染 |
| **总计** | **< 90ms** | |

### 8.2 内存管理

```
┌─────────────────────────────────────────────────────────────────┐
│                     Memory Management                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  编码端 (被控端):                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Frame Pool (3-5 帧)                                     │   │
│  │  ├─ 减少内存分配开销                                     │   │
│  │  └─ 支持异步编码管线                                     │   │
│  │                                                          │   │
│  │  AVPacket Pool                                           │   │
│  │  ├─ 复用编码输出缓冲                                     │   │
│  │  └─ 避免频繁 malloc/free                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  解码端 (控制端):                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  HW Frames Pool (10-15 帧)                               │   │
│  │  ├─ FFmpeg 自动管理                                      │   │
│  │  ├─ initial_pool_size 需要足够大                         │   │
│  │  └─ 考虑 JitterBuffer 延迟                               │   │
│  │                                                          │   │
│  │  SRV Cache (D3D11 渲染器)                                │   │
│  │  ├─ 避免每帧创建 SRV                                     │   │
│  │  └─ Seek 时需要清理                                      │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 8.3 线程模型

```
┌─────────────────────────────────────────────────────────────────┐
│                       Thread Model                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  编码端 (被控端):                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Capture Thread                                          │   │
│  │  └─ DXGI 采集 → Frame Queue                              │   │
│  │                                                          │   │
│  │  Encode Thread                                           │   │
│  │  └─ Frame Queue → 编码 → AVPacket Queue                  │   │
│  │                                                          │   │
│  │  Send Thread (loki TaskRunner)                           │   │
│  │  └─ AVPacket Queue → RTP 打包 → UDP 发送                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  解码端 (控制端):                                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Receive Thread (loki TaskRunner)                        │   │
│  │  └─ UDP 接收 → RTP 解包 → JitterBuffer                   │   │
│  │                                                          │   │
│  │  Decode Thread                                           │   │
│  │  └─ JitterBuffer → 解码 → Frame Queue                    │   │
│  │                                                          │   │
│  │  Render Thread (UI Thread)                               │   │
│  │  └─ Frame Queue → 渲染 → Present                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 8.4 关键性能指标监控

```cpp
struct MediaPipelineStats {
    // 编码端统计
    struct EncoderStats {
        uint64_t frames_encoded = 0;
        uint64_t bytes_encoded = 0;
        double avg_encode_time_ms = 0;
        double max_encode_time_ms = 0;
        uint32_t current_bitrate_bps = 0;
        bool is_hw_encoder = false;
    };
    
    // 解码端统计
    struct DecoderStats {
        uint64_t frames_decoded = 0;
        uint64_t frames_dropped = 0;
        double avg_decode_time_ms = 0;
        double max_decode_time_ms = 0;
        bool is_hw_decoder = false;
    };
    
    // 渲染统计
    struct RenderStats {
        uint64_t frames_rendered = 0;
        double avg_render_time_ms = 0;
        double fps = 0;
        bool is_zero_copy = false;
    };
    
    // 端到端延迟
    double end_to_end_latency_ms = 0;
};
```

---

## 9. 兼容性考虑

### 9.1 操作系统兼容性

| 功能 | Windows 7 | Windows 8/8.1 | Windows 10/11 |
|------|-----------|---------------|---------------|
| DXGI 采集 | ❌ 不支持 | ✅ 支持 | ✅ 支持 |
| D3D11VA 解码 | ❌ 部分支持 | ✅ 支持 | ✅ 支持 |
| NVENC | ✅ 支持 | ✅ 支持 | ✅ 支持 |
| QSV | ✅ 支持 | ✅ 支持 | ✅ 支持 |
| AMF | ❌ 不支持 | ✅ 支持 | ✅ 支持 |

### 9.2 GPU 兼容性

| GPU 厂商 | 硬件编码 | 硬件解码 | 零拷贝渲染 |
|---------|---------|---------|-----------|
| **NVIDIA** (Kepler+) | NVENC ✅ | NVDEC/D3D11VA ✅ | D3D11 ✅ |
| **Intel** (6代+) | QSV ✅ | QSV/D3D11VA ✅ | D3D11 ✅ |
| **AMD** (GCN+) | AMF ✅ | VCE/D3D11VA ✅ | D3D11 ✅ |
| **集成显卡** | 取决于型号 | D3D11VA ✅ | D3D11 ✅ |

### 9.3 FFmpeg 版本兼容性

| FFmpeg 版本 | 支持状态 | 备注 |
|------------|---------|------|
| 4.x | ✅ 支持 | 需要特定 configure 选项 |
| 5.x | ✅ 支持 (推荐) | API 稳定 |
| 6.x | ✅ 支持 | 最新特性 |

**编译要求**：
```bash
# FFmpeg 编译选项
./configure \
    --enable-d3d11va \
    --enable-nvenc \
    --enable-libx264 \
    --enable-gpl \
    --enable-shared
```

---

## 10. 扩展性考虑

### 10.1 编解码器扩展

```cpp
// 编码器注册机制
class EncoderRegistry {
public:
    static EncoderRegistry& Instance();
    
    // 注册编码器工厂
    void Register(EncoderType type, 
                  std::function<std::unique_ptr<IVideoEncoder>()> factory);
    
    // 创建编码器
    std::unique_ptr<IVideoEncoder> Create(EncoderType type);
    
    // 获取所有可用编码器
    std::vector<EncoderType> GetAvailableEncoders();
};

// 使用示例：添加 H.265 支持
EncoderRegistry::Instance().Register(
    EncoderType::kHEVC_NVENC,
    []() { return std::make_unique<HEVCNVENCEncoder>(); }
);
```

### 10.2 渲染器扩展

```cpp
// 渲染器注册机制
class RendererRegistry {
public:
    static RendererRegistry& Instance();
    
    void Register(const std::string& name,
                  std::function<std::unique_ptr<Renderer>()> factory,
                  RendererCapabilities caps);
    
    std::unique_ptr<Renderer> CreateBest(const RendererRequirements& req);
};

// 未来扩展：Vulkan 渲染器
RendererRegistry::Instance().Register(
    "Vulkan",
    []() { return std::make_unique<VulkanRenderer>(); },
    {.supports_zero_copy = true, .platforms = {Windows, Linux}}
);
```

### 10.3 平台扩展 (Phase 5 准备)

```cpp
// 平台抽象层
namespace platform {

// 屏幕采集抽象
class IScreenCapturer {
public:
    virtual ~IScreenCapturer() = default;
    virtual Result<void> Start(const CaptureConfig& config) = 0;
    virtual std::optional<Frame> CaptureFrame() = 0;
    virtual void Stop() = 0;
};

// 工厂函数
std::unique_ptr<IScreenCapturer> CreateScreenCapturer();
// Windows: 返回 DXGIScreenCapturer
// macOS:   返回 SCKitScreenCapturer

// 输入注入抽象
class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual void InjectMouseMove(int x, int y) = 0;
    virtual void InjectMouseClick(MouseButton btn, bool down) = 0;
    virtual void InjectKeyEvent(uint32_t keycode, bool down) = 0;
};

std::unique_ptr<IInputInjector> CreateInputInjector();
// Windows: 返回 Win32InputInjector (SendInput)
// macOS:   返回 CGEventInputInjector

}  // namespace platform
```

---

## 11. 错误处理设计

### 11.1 错误恢复策略

```
┌─────────────────────────────────────────────────────────────────┐
│                    Error Recovery Strategy                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  编码器错误:                                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  硬件编码器失败                                          │   │
│  │  └─ 尝试其他硬件编码器                                   │   │
│  │     └─ 全部失败 → 降级到软件编码                         │   │
│  │                                                          │   │
│  │  编码超时/卡死                                           │   │
│  │  └─ 重置编码器 → 请求 I 帧                               │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  解码器错误:                                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  硬件解码器失败                                          │   │
│  │  └─ 降级到软件解码                                       │   │
│  │                                                          │   │
│  │  解码损坏帧                                              │   │
│  │  └─ 丢弃当前帧 → 等待下一个 I 帧                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  渲染器错误:                                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  D3D11 设备丢失 (Device Lost)                            │   │
│  │  └─ 重新创建设备和交换链                                 │   │
│  │                                                          │   │
│  │  SRV 创建失败                                            │   │
│  │  └─ 清理缓存 → 重试                                      │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 11.2 关键帧请求机制

```cpp
// 当检测到解码错误或网络丢包时，请求发送端发送 I 帧
class KeyFrameRequester {
public:
    void RequestKeyFrame();
    
    // 节流：避免频繁请求
    void RequestKeyFrameThrottled(int min_interval_ms = 1000);
    
private:
    std::chrono::steady_clock::time_point last_request_;
};

// 编码端响应
void VideoEncoder::OnKeyFrameRequest() {
    // 强制下一帧为 I 帧
    force_key_frame_ = true;
}
```

---

## 12. 测试策略

### 12.1 单元测试

| 测试项 | 测试内容 |
|-------|---------|
| 编码器测试 | 各种分辨率、帧率、码率配置 |
| 解码器测试 | 正常流、损坏流、I帧恢复 |
| 渲染器测试 | 初始化、渲染、窗口大小变化 |
| 硬件检测测试 | 各厂商 GPU 检测逻辑 |

### 12.2 集成测试

```
┌─────────────────────────────────────────────────────────────────┐
│                   Integration Test Scenarios                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  场景 1: 本地回环测试                                            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Capture → Encode → Decode → Render (同一进程)          │   │
│  │  验证: 端到端延迟、画质、稳定性                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  场景 2: 硬件/软件组合测试                                       │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  HW Encode + HW Decode (最优路径)                        │   │
│  │  HW Encode + SW Decode                                   │   │
│  │  SW Encode + HW Decode                                   │   │
│  │  SW Encode + SW Decode (最差路径)                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  场景 3: 错误恢复测试                                            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  模拟编码器崩溃 → 验证自动重启                           │   │
│  │  模拟解码错误 → 验证 I 帧请求                            │   │
│  │  模拟设备丢失 → 验证渲染器重建                           │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 12.3 性能测试

| 测试项 | 目标指标 |
|-------|---------|
| 编码延迟 | < 10ms (HW), < 30ms (SW) |
| 解码延迟 | < 10ms (HW), < 20ms (SW) |
| 渲染延迟 | < 5ms |
| 端到端延迟 | < 100ms (LAN) |
| CPU 占用 | < 20% (HW 全路径) |
| GPU 占用 | < 50% (4K@30fps) |

---

## 13. 实施计划

### Week 1: 视频编码器

| 天数 | 任务 |
|------|------|
| Day 1 | 编码器接口设计、EncoderConfig 定义 |
| Day 2 | 软件编码器 (libx264) 实现 |
| Day 3 | 色彩空间转换 (BGRA → NV12) |
| Day 4 | NVENC 硬件编码器实现 |
| Day 5 | QSV/AMF 编码器实现、硬件检测逻辑 |

### Week 2: 视频解码器 + 渲染器

| 天数 | 任务 |
|------|------|
| Day 1 | 从 zenplay 迁移 Decoder 基类 |
| Day 2 | 从 zenplay 迁移 VideoDecoder、HWDecoderContext |
| Day 3 | 从 zenplay 迁移 SDLRenderer |
| Day 4 | 从 zenplay 迁移 D3D11Renderer |
| Day 5 | 集成测试：Encode → Decode → Render 全链路 |

---

## 14. 附录

### 14.1 参考资料

- [FFmpeg Hardware Acceleration](https://trac.ffmpeg.org/wiki/HWAccelIntro)
- [NVIDIA Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [Intel Media SDK](https://github.com/Intel-Media-SDK/MediaSDK)
- [AMD AMF SDK](https://github.com/GPUOpen-LibrariesAndSDKs/AMF)
- [zenplay 项目](https://github.com/Sunshine334419520/zenplay)

### 14.2 术语表

| 术语 | 说明 |
|------|------|
| NVENC | NVIDIA Encoder - NVIDIA GPU 硬件编码器 |
| QSV | Quick Sync Video - Intel 集成显卡硬件编解码 |
| AMF | Advanced Media Framework - AMD GPU 硬件编解码 |
| D3D11VA | Direct3D 11 Video Acceleration - Windows 硬件解码 API |
| NV12 | YUV 4:2:0 格式，UV 交织存储 |
| SRV | Shader Resource View - D3D11 着色器资源视图 |
| SwapChain | 交换链 - 管理前后缓冲区的 D3D11 组件 |

---

**文档版本**: v1.0  
**创建日期**: 2025-01-04  
**最后更新**: 2025-01-04
