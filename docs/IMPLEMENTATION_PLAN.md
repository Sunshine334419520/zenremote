# ZenRemote 功能完善实施计划

## 1. 项目现状分析

### 1.1 已完成模块

| 模块 | 目录 | 状态 | 说明 |
|------|------|------|------|
| **Network 层** | `src/network/` | ✅ 已完成 | UDP Socket、RTP 发送/接收、可靠传输、JitterBuffer、Pacer |
| **Transport 层** | `src/transport/` | ✅ 已完成 | PeerConnection、DataChannel、VideoTrack、AudioTrack |
| **Session 层** | `src/app/session/` | ⚠️ 部分完成 | ControllerSession、ControlledSession 基础框架已有 |
| **屏幕采集** | `src/media/capture/` | ✅ 已完成 | DXGI Desktop Duplication 屏幕采集 |
| **公共模块** | `src/common/` | ✅ 已完成 | 错误处理、日志、配置管理 |

### 1.2 待实现模块

| 模块 | 目录 | 状态 | 优先级 |
|------|------|------|--------|
| **UI** | `src/app/ui/` | ❌ 未开始 | P0 - 最高 |
| **视频编码器** | `src/media/codec/` | ❌ 未开始 | P0 - 最高 |
| **视频解码器** | `src/media/codec/` | ❌ 未开始 | P0 - 最高 |
| **视频渲染器** | `src/media/renderer/` | ❌ 未开始 | P0 - 最高 |
| **输入控制** | `src/control/` | ❌ 未开始 | P1 - 高 |
| **音频编解码** | `src/media/codec/` | ❌ 未开始 | P2 - 中 |

---

## 2. 实施计划概览

### 2.1 阶段划分

```
┌─────────────────────────────────────────────────────────────────┐
│ Phase 1: 核心媒体管线 - Windows (2 周)                            │
│   Week 1: 视频编解码器                                           │
│   Week 2: 视频渲染器                                             │
├─────────────────────────────────────────────────────────────────┤
│ Phase 2: UI 与集成测试 - Windows (1 周)                           │
│   - 简单的测试 UI                                                │
│   - 端到端集成测试                                               │
├─────────────────────────────────────────────────────────────────┤
│ Phase 3: 输入控制 - Windows (1 周)                                │
│   - 鼠标键盘捕获                                                 │
│   - 输入事件注入                                                 │
├─────────────────────────────────────────────────────────────────┤
│ Phase 4: 优化与完善 - Windows (1 周)                              │
│   - 性能优化                                                     │
│   - 错误处理完善                                                 │
├─────────────────────────────────────────────────────────────────┤
│ Phase 5: macOS 平台支持 (3-4 周)                                  │
│   Week 1: 屏幕采集 (SCStreamCaptureKit)                          │
│   Week 2: 视频编解码 (VideoToolbox)                              │
│   Week 3: 输入控制 (CGEvent)                                     │
│   Week 4: 跨平台联调测试                                         │
├─────────────────────────────────────────────────────────────────┤
│ Phase 6: 公网支持 - TURN 服务器 (2-3 周)                          │
│   Week 1: 信令服务器 (WebSocket)                                 │
│   Week 2: TURN 客户端实现                                        │
│   Week 3: 连接管理与自动切换                                     │
├─────────────────────────────────────────────────────────────────┤
│ Phase 7: 性能检测与优化 (2 周)                                    │
│   Week 1: Stats 统计系统                                         │
│   Week 2: 自适应优化与诊断工具                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Phase 1: 核心媒体管线

### 3.1 视频编码器 (Week 1 前半)

#### 3.1.1 设计目标

- 支持 H.264 编码（远程桌面标准）
- 支持硬件编码（NVENC/QSV/AMF）和软件编码（libx264）后备
- 低延迟模式（tune=zerolatency）
- 适配屏幕采集输出的 BGRA 格式

#### 3.1.2 类设计

```cpp
// src/media/codec/video_encoder.h

class VideoEncoder {
 public:
  struct Config {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 2000000;  // 2 Mbps
    bool use_hw_accel = true;
    std::string preset = "ultrafast";  // 低延迟
  };

  VideoEncoder();
  ~VideoEncoder();

  Result<void> Open(const Config& config);
  void Close();

  // 编码一帧，输出编码后的 AVPacket 列表
  Result<std::vector<AVPacketPtr>> Encode(AVFrame* frame);
  
  // 刷新编码器缓冲
  Result<std::vector<AVPacketPtr>> Flush();

  bool IsHardwareEncoding() const;

 private:
  Result<void> InitHardwareEncoder(const Config& config);
  Result<void> InitSoftwareEncoder(const Config& config);
  Result<void> SetupColorConversion(AVPixelFormat src_fmt);

  std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
  std::unique_ptr<SwsContext, SwsContextDeleter> sws_context_;
  AVFramePtr converted_frame_;
  bool use_hw_accel_ = false;
};
```

#### 3.1.3 硬件编码器优先级

```
Windows:
  1. NVENC (h264_nvenc) - NVIDIA GPU
  2. QSV (h264_qsv) - Intel 集成显卡
  3. AMF (h264_amf) - AMD GPU
  4. libx264 (软件后备)
```

#### 3.1.4 关键实现点

1. **色彩空间转换**：屏幕采集输出 BGRA，编码器需要 NV12/YUV420P
   ```cpp
   // BGRA -> NV12 转换
   sws_context_ = sws_getContext(
       width, height, AV_PIX_FMT_BGRA,
       width, height, AV_PIX_FMT_NV12,
       SWS_BILINEAR, nullptr, nullptr, nullptr);
   ```

2. **低延迟配置**：
   ```cpp
   av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
   av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
   codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
   codec_ctx->max_b_frames = 0;  // 禁用 B 帧
   ```

3. **关键帧间隔**：
   ```cpp
   codec_ctx->gop_size = fps * 2;  // 每 2 秒一个 I 帧
   ```

---

### 3.2 视频解码器 (Week 1 后半)

#### 3.2.1 设计目标

- 支持 H.264 解码
- 支持硬件解码（D3D11VA）和软件解码后备
- 零拷贝渲染支持（硬件解码帧直接渲染）

#### 3.2.2 参考 zenplay 实现

从 zenplay 项目迁移以下核心文件：

```
zenplay/src/player/codec/
├── decode.h/cpp           # Decoder 基类
├── video_decoder.h/cpp    # VideoDecoder 实现
├── hw_decoder_context.h/cpp  # 硬件解码上下文
└── hw_decoder_type.h/cpp  # 硬件解码器类型定义
```

#### 3.2.3 关键修改点

1. **命名空间**：`zenplay` → `zenremote`
2. **日志系统**：适配 zenremote 的 spdlog 封装
3. **错误处理**：适配 zenremote 的 `Result<T>` 模式
4. **简化**：移除播放器特有逻辑（Seek、AV Sync 等）

#### 3.2.4 类设计（与 zenplay 一致）

```cpp
// src/media/codec/video_decoder.h

class VideoDecoder : public Decoder {
 public:
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr,
                    HWDecoderContext* hw_context = nullptr);

  bool IsHardwareDecoding() const;
  HWDecoderContext* GetHWContext() const;
  
  Result<AVFrame*> ReceiveFrame() override;

  // 视频属性访问
  int width() const;
  int height() const;
  AVPixelFormat pixel_format() const;

 protected:
  Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) override;

 private:
  HWDecoderContext* hw_context_ = nullptr;
  bool zero_copy_validated_ = false;
};
```

---

### 3.3 视频渲染器 (Week 2)

#### 3.3.1 设计目标

- 支持 SDL 软件渲染（跨平台）
- 支持 D3D11 硬件渲染（Windows，零拷贝）
- 自动选择最优渲染路径

#### 3.3.2 参考 zenplay 实现

从 zenplay 项目迁移以下核心文件：

```
zenplay/src/player/video/render/
├── renderer.h             # Renderer 抽象接口
├── impl/sdl/
│   ├── sdl_renderer.h/cpp # SDL 渲染器
│   └── sdl_manager.h/cpp  # SDL 生命周期管理
└── impl/d3d11/
    ├── d3d11_renderer.h/cpp  # D3D11 渲染器
    ├── d3d11_context.h/cpp   # D3D11 设备上下文
    ├── d3d11_shader.h/cpp    # 着色器管理
    └── d3d11_swap_chain.h/cpp # 交换链管理
```

#### 3.3.3 渲染器接口

```cpp
// src/media/renderer/renderer.h

class Renderer {
 public:
  virtual ~Renderer() = default;

  virtual Result<void> Init(void* window_handle, int width, int height) = 0;
  virtual bool RenderFrame(AVFrame* frame) = 0;
  virtual void Clear() = 0;
  virtual void Present() = 0;
  virtual void OnResize(int width, int height) = 0;
  virtual void Cleanup() = 0;
  virtual const char* GetRendererName() const = 0;
};
```

#### 3.3.4 渲染路径选择

```cpp
// 根据解码模式选择渲染器
std::unique_ptr<Renderer> CreateRenderer(bool use_hw_decode) {
  if (use_hw_decode) {
    // 硬件解码 → D3D11 渲染器（零拷贝）
    return std::make_unique<D3D11Renderer>();
  } else {
    // 软件解码 → SDL 渲染器
    return std::make_unique<SDLRenderer>();
  }
}
```

---

## 4. Phase 2: UI 与集成测试

### 4.1 测试 UI 设计

#### 4.1.1 设计目标

- **极简**：快速验证功能，不追求美观
- **双角色**：同一程序可作为控制端或被控端
- **单机测试**：支持在一台机器上同时运行两个实例

#### 4.1.2 UI 方案选择

**方案 A：Qt 窗口（推荐）**
- 优点：项目已有 Qt6 依赖，开发快速
- 缺点：UI 代码较多

**方案 B：纯命令行 + SDL 窗口**
- 优点：代码最少
- 缺点：调试不便

**推荐方案 A**，UI 布局如下：

```
┌─────────────────────────────────────────────────────────┐
│  ZenRemote - 远程桌面测试                               │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  本机信息:                                              │
│  ┌─────────────────────────────────────────────────┐   │
│  │  设备 ID: 123456                                │   │
│  │  连接密码: abcd1234                             │   │
│  │  监听端口: 50000                                │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ─────────────────────────────────────────────────────  │
│                                                         │
│  连接到远程:                                            │
│  ┌─────────────────────────────────────────────────┐   │
│  │  远程 IP: [_______________]                     │   │
│  │  远程端口: [50000______]                        │   │
│  │  密码: [_______________]                        │   │
│  │                                                 │   │
│  │  [连接]  [断开]                                 │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  状态: 等待连接...                                      │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

#### 4.1.3 单机测试策略

由于在同一台机器上测试，需要区分控制端和被控端实例：

```
实例 A（被控端）:
  - 监听端口: 50000
  - 屏幕采集 → 编码 → 发送
  - 接收输入事件 → 注入

实例 B（控制端）:
  - 监听端口: 50001
  - 接收视频流 → 解码 → 渲染
  - 捕获输入 → 发送
```

命令行参数支持：
```bash
# 启动被控端
zenremote.exe --mode controlled --port 50000

# 启动控制端（连接到本机被控端）
zenremote.exe --mode controller --remote 127.0.0.1:50000
```

#### 4.1.4 视频显示窗口

控制端需要一个独立窗口显示远程桌面：

```cpp
// 使用 SDL 创建独立渲染窗口
class RemoteDesktopWindow {
 public:
  bool Create(int width, int height);
  void Render(AVFrame* frame);
  void HandleInput();  // 捕获输入事件
  void Close();

 private:
  SDL_Window* window_ = nullptr;
  std::unique_ptr<Renderer> renderer_;
};
```

---

## 5. Phase 3: 输入控制 (Windows)

### 5.1 输入捕获

#### 5.1.1 鼠标事件捕获

```cpp
// src/app/control/input_capturer_win.h

class InputCapturerWin {
 public:
  using InputCallback = std::function<void(const InputEvent&)>;

  bool Start(HWND target_window, InputCallback callback);
  void Stop();

 private:
  static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);

  HHOOK mouse_hook_ = nullptr;
  HHOOK keyboard_hook_ = nullptr;
  InputCallback callback_;
};
```

#### 5.1.2 键盘事件捕获

Windows 使用低级键盘钩子（`WH_KEYBOARD_LL`）捕获按键。

### 5.2 输入注入

#### 5.2.1 鼠标注入

```cpp
// src/app/control/input_injector_win.cpp

void InjectMouseMove(int x, int y) {
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dx = x * 65535 / GetSystemMetrics(SM_CXSCREEN);
  input.mi.dy = y * 65535 / GetSystemMetrics(SM_CYSCREEN);
  input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  SendInput(1, &input, sizeof(INPUT));
}

void InjectMouseClick(int button, bool down) {
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  SendInput(1, &input, sizeof(INPUT));
}
```

#### 5.2.2 键盘注入

```cpp
void InjectKeyEvent(uint32_t vk_code, bool down) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = static_cast<WORD>(vk_code);
  input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &input, sizeof(INPUT));
}
```

---

## 6. 详细实施时间表

### Week 1: 视频编解码器

| 天数 | 任务 | 产出 |
|------|------|------|
| Day 1 | 视频编码器框架搭建 | `video_encoder.h/cpp` 基础结构 |
| Day 2 | 软件编码器实现（libx264） | 软件编码可工作 |
| Day 3 | 硬件编码器实现（NVENC/QSV） | 硬件编码可工作 |
| Day 4 | 从 zenplay 迁移解码器代码 | 解码器基础类迁移完成 |
| Day 5 | 解码器适配与测试 | 软解/硬解测试通过 |

### Week 2: 视频渲染器

| 天数 | 任务 | 产出 |
|------|------|------|
| Day 1 | 从 zenplay 迁移 SDL 渲染器 | SDL 渲染器可工作 |
| Day 2 | 从 zenplay 迁移 D3D11 渲染器 | D3D11 渲染器框架完成 |
| Day 3 | D3D11 零拷贝渲染调试 | 硬件解码 → D3D11 渲染通路打通 |
| Day 4 | 渲染器集成测试 | 编码 → 解码 → 渲染 全链路测试 |
| Day 5 | Bug 修复与优化 | 渲染稳定 |

### Week 3: UI 与集成测试

| 天数 | 任务 | 产出 |
|------|------|------|
| Day 1 | Qt UI 框架搭建 | 主窗口布局完成 |
| Day 2 | 连接管理逻辑 | 连接/断开功能 |
| Day 3 | 远程桌面显示窗口 | 视频流显示正常 |
| Day 4 | Session 层集成 | Controller/Controlled Session 完善 |
| Day 5 | 端到端测试 | 单机双实例测试通过 |

### Week 4: 输入控制

| 天数 | 任务 | 产出 |
|------|------|------|
| Day 1 | 输入捕获实现 | 鼠标/键盘事件捕获 |
| Day 2 | 输入注入实现 | 鼠标/键盘事件注入 |
| Day 3 | 输入事件协议集成 | 通过 DataChannel 传输 |
| Day 4 | 输入控制测试 | 远程控制功能测试 |
| Day 5 | Bug 修复与文档 | Phase 1-4 Windows 版本验收 |

### Phase 5: macOS 平台支持

| 周数 | 任务 | 产出 |
|------|------|------|
| **Week 1** | **屏幕采集** | |
| Day 1-2 | SCStreamCaptureKit 集成 | macOS 屏幕采集框架 |
| Day 3-4 | 屏幕帧格式转换 | CVPixelBuffer → AVFrame |
| Day 5 | 采集测试与调试 | 采集功能验证通过 |
| **Week 2** | **视频编解码** | |
| Day 1-2 | VideoToolbox 硬件编码器 | macOS 硬件编码支持 |
| Day 3-4 | VideoToolbox 硬件解码器 | macOS 硬件解码支持 |
| Day 5 | 编解码测试 | 编解码链路通过 |
| **Week 3** | **输入控制** | |
| Day 1-2 | CGEvent 输入注入 | 鼠标/键盘事件注入 |
| Day 3-4 | 输入事件捕获 | NSEvent 监听 |
| Day 5 | 输入控制集成 | 输入功能完整 |
| **Week 4** | **跨平台联调** | |
| Day 1-2 | Windows → macOS 控制测试 | 跨平台控制验证 |
| Day 3-4 | macOS → Windows 控制测试 | 双向控制验证 |
| Day 5 | Bug 修复与优化 | Phase 5 完成验收 |

### Phase 6: 公网支持 - TURN 服务器

| 周数 | 任务 | 产出 |
|------|------|------|
| **Week 1** | **信令服务器** | |
| Day 1-2 | WebSocket 服务器搭建 | 信令服务基础框架 |
| Day 3-4 | 连接信息交换协议 | 设备注册/发现/连接协商 |
| Day 5 | 客户端信令集成 | 客户端能连接信令服务 |
| **Week 2** | **TURN 客户端** | |
| Day 1-2 | TURN 协议实现 (RFC 5766) | Allocate/Refresh/Permission |
| Day 3-4 | TURN 数据转发 | ChannelBind/Send/Data |
| Day 5 | TURN 客户端测试 | 中继通信验证 |
| **Week 3** | **连接管理** | |
| Day 1-2 | ConnectionManager 重构 | 统一直连/中继管理 |
| Day 3-4 | 自动路径选择 | 直连优先,失败自动中继 |
| Day 5 | 公网测试与验收 | Phase 6 完成验收 |

### Phase 7: 性能检测与优化

| 周数 | 任务 | 产出 |
|------|------|------|
| **Week 1** | **Stats 统计系统** | |
| Day 1 | Stats 数据模型设计 | StatsReport 类型定义 |
| Day 2 | 发送端统计收集 | 编码/发送/Pacer 指标 |
| Day 3 | 接收端统计收集 | 解码/渲染/JitterBuffer 指标 |
| Day 4 | 网络质量统计 | RTT/丢包率/带宽估计 |
| Day 5 | Stats API 封装 | GetStats() 接口完成 |
| **Week 2** | **自适应优化与诊断** | |
| Day 1 | 自适应码率控制 | 根据网络状况动态调整 |
| Day 2 | 自适应帧率控制 | 根据 CPU/网络动态调整 |
| Day 3 | 性能诊断工具 | 实时 Stats 展示 UI |
| Day 4 | 性能日志与导出 | Stats 日志记录与分析 |
| Day 5 | 端到端延迟优化 | 全链路延迟测量与优化 |

---

## 7. 文件结构规划

```
src/
├── media/
│   ├── codec/
│   │   ├── decoder.h/cpp           # 解码器基类（迁移自 zenplay）
│   │   ├── video_decoder.h/cpp     # 视频解码器（迁移自 zenplay）
│   │   ├── video_encoder.h/cpp     # 视频编码器（新实现）
│   │   ├── hw_decoder_context.h/cpp # 硬件解码上下文（迁移自 zenplay）
│   │   └── hw_decoder_type.h/cpp   # 硬件解码类型（迁移自 zenplay）
│   ├── renderer/
│   │   ├── renderer.h              # 渲染器接口（迁移自 zenplay）
│   │   ├── sdl_renderer.h/cpp      # SDL 渲染器（迁移自 zenplay）
│   │   ├── sdl_manager.h/cpp       # SDL 管理器（迁移自 zenplay）
│   │   ├── d3d11_renderer.h/cpp    # D3D11 渲染器（迁移自 zenplay）
│   │   ├── d3d11_context.h/cpp     # D3D11 上下文（迁移自 zenplay）
│   │   ├── d3d11_shader.h/cpp      # D3D11 着色器（迁移自 zenplay）
│   │   └── d3d11_swap_chain.h/cpp  # D3D11 交换链（迁移自 zenplay）
│   └── capture/
│       ├── screen_capturer.h       # 屏幕采集接口（已有）
│       ├── screen_capturer_win.cpp # Windows DXGI 采集（已有）
│       └── screen_capturer_mac.mm  # macOS SCStreamCaptureKit 采集（Phase 5 新增）
├── app/
│   ├── control/                    # 输入控制模块（从 src/control 移至此处）
│   │   ├── input_event.h           # 输入事件定义
│   │   ├── input_capturer.h        # 输入捕获接口
│   │   ├── input_capturer_win.cpp  # Windows 输入捕获
│   │   ├── input_capturer_mac.mm   # macOS 输入捕获（Phase 5 新增）
│   │   ├── input_injector.h        # 输入注入接口
│   │   ├── input_injector_win.cpp  # Windows 输入注入
│   │   └── input_injector_mac.mm   # macOS 输入注入（Phase 5 新增）
│   ├── ui/
│   │   ├── main_window.h/cpp       # 主窗口
│   │   ├── remote_desktop_widget.h/cpp # 远程桌面显示控件
│   │   └── connection_dialog.h/cpp # 连接对话框
│   └── session/                    # 会话管理（已有）
│       ├── controller_session.h/cpp
│       └── controlled_session.h/cpp
├── network/
│   ├── connection/
│   │   ├── direct_connection.h/cpp # 局域网直连（已有）
│   │   ├── turn_connection.h/cpp   # TURN 中继连接（Phase 6 新增）
│   │   └── connection_manager.h/cpp # 统一连接管理（Phase 6 新增）
│   └── signaling/                  # 信令模块（Phase 6 新增）
│       ├── signaling_client.h/cpp  # WebSocket 信令客户端
│       └── signaling_protocol.h    # 信令协议定义
└── transport/                      # 传输层（已有）
    └── ...
```

---

## 8. 关键依赖说明

### 8.1 FFmpeg 依赖

已有依赖，需确保以下库可用：
- `libavcodec` - 编解码
- `libavutil` - 工具函数
- `libswscale` - 色彩空间转换

### 8.2 SDL2 依赖

需新增依赖（通过 Conan）：

```python
# conanfile.py
def requirements(self):
    # 现有依赖...
    self.requires("sdl/2.28.5")  # 新增
```

### 8.3 Qt6 依赖

已有依赖（Core, Widgets, Gui）。

---

## 9. 风险与缓解措施

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 硬件编码器不可用 | 编码性能下降 | 优雅降级到软件编码 |
| D3D11 渲染器兼容性 | 某些显卡不支持 | 回退到 SDL 渲染 |
| 输入钩子被杀毒拦截 | 无法捕获输入 | 添加白名单说明文档 |
| 单机测试网络回环性能 | 无法模拟真实场景 | 后续使用双机测试 |
| macOS 权限问题 | 屏幕采集/输入注入需要权限 | 引导用户授予权限，代码中检测权限状态 |
| VideoToolbox 编码器限制 | 部分旧 Mac 不支持 | 软件编码后备方案 |
| TURN 服务器成本 | 流量费用 | 直连优先策略，仅跨网段使用中继 |
| 网络抖动导致延迟 | 用户体验下降 | JitterBuffer 动态调整 |

---

## 10. Phase 5: macOS 平台详细设计

### 10.1 屏幕采集 (SCStreamCaptureKit)

macOS 12.3+ 推荐使用 `ScreenCaptureKit` 框架，替代已废弃的 `CGDisplayStream`。

```objc
// src/media/capture/screen_capturer_mac.mm

@interface ScreenCapturerMac : NSObject <SCStreamDelegate, SCStreamOutput>
- (BOOL)startCaptureWithDisplay:(CGDirectDisplayID)displayID
                     frameRate:(NSInteger)fps
                      callback:(void(^)(CVPixelBufferRef))callback;
- (void)stopCapture;
@end

@implementation ScreenCapturerMac {
    SCStream* _stream;
    SCStreamConfiguration* _config;
    dispatch_queue_t _captureQueue;
}

- (BOOL)startCaptureWithDisplay:(CGDirectDisplayID)displayID
                     frameRate:(NSInteger)fps
                      callback:(void(^)(CVPixelBufferRef))callback {
    // 1. 创建内容过滤器 (指定采集的显示器)
    SCContentFilter* filter = [[SCContentFilter alloc] 
        initWithDisplay:displayID excludingWindows:@[]];
    
    // 2. 配置采集参数
    _config = [[SCStreamConfiguration alloc] init];
    _config.width = displayWidth;
    _config.height = displayHeight;
    _config.minimumFrameInterval = CMTimeMake(1, fps);
    _config.pixelFormat = kCVPixelFormatType_32BGRA;
    
    // 3. 创建流并启动
    _stream = [[SCStream alloc] initWithFilter:filter 
                                 configuration:_config 
                                      delegate:self];
    [_stream addStreamOutput:self type:SCStreamOutputTypeScreen 
             sampleHandlerQueue:_captureQueue error:nil];
    [_stream startCaptureWithCompletionHandler:nil];
    return YES;
}

- (void)stream:(SCStream*)stream 
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
                   ofType:(SCStreamOutputType)type {
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    // 回调给 C++ 层处理
    if (_callback) _callback(pixelBuffer);
}
@end
```

### 10.2 视频编码 (VideoToolbox)

```objc
// src/media/codec/video_encoder_mac.mm

class VideoEncoderMac {
public:
    Result<void> Open(const Config& config) {
        // 创建 H.264 硬件编码会话
        VTCompressionSessionRef session;
        OSStatus status = VTCompressionSessionCreate(
            kCFAllocatorDefault,
            config.width, config.height,
            kCMVideoCodecType_H264,
            nullptr,  // 使用默认编码器
            nullptr,  // 使用默认像素缓冲池
            nullptr,  // 使用默认压缩分配器
            CompressionOutputCallback,
            this,
            &session);
        
        // 配置低延迟
        VTSessionSetProperty(session, 
            kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        VTSessionSetProperty(session,
            kVTCompressionPropertyKey_ProfileLevel,
            kVTProfileLevel_H264_Main_AutoLevel);
        VTSessionSetProperty(session,
            kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
        
        return Ok();
    }
    
    Result<std::vector<AVPacketPtr>> Encode(CVPixelBufferRef pixelBuffer) {
        CMTime pts = CMTimeMake(frame_count_++, fps_);
        VTCompressionSessionEncodeFrame(session_, pixelBuffer, pts, 
            kCMTimeInvalid, nullptr, nullptr, nullptr);
        return encoded_packets_;
    }
};
```

### 10.3 输入控制 (CGEvent)

```objc
// src/app/control/input_injector_mac.mm

void InjectMouseMove(int x, int y) {
    CGEventRef event = CGEventCreateMouseEvent(
        nullptr, kCGEventMouseMoved, 
        CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void InjectMouseClick(int button, bool down) {
    CGEventType eventType;
    if (button == 0) {  // Left button
        eventType = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
    } else {  // Right button
        eventType = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
    }
    CGEventRef event = CGEventCreateMouseEvent(
        nullptr, eventType, currentMousePos, button);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void InjectKeyEvent(uint16_t keyCode, bool down) {
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, keyCode, down);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}
```

### 10.4 macOS 权限要求

| 权限 | 用途 | 获取方式 |
|------|------|---------|
| Screen Recording | 屏幕采集 | 系统偏好设置 → 安全与隐私 → 隐私 → 屏幕录制 |
| Accessibility | 输入注入 | 系统偏好设置 → 安全与隐私 → 隐私 → 辅助功能 |

```objc
// 检查权限
BOOL hasScreenRecordingPermission() {
    if (@available(macOS 11.0, *)) {
        return CGPreflightScreenCaptureAccess();
    }
    return YES;  // 旧版本无需权限
}

BOOL hasAccessibilityPermission() {
    return AXIsProcessTrusted();
}
```

---

## 11. Phase 6: 公网支持详细设计

### 11.1 信令服务器

信令服务器负责设备注册、发现和连接协商，使用 WebSocket 长连接。

#### 11.1.1 信令协议

```json
// 设备注册
{
    "type": "register",
    "device_id": "abc123",
    "device_name": "My PC",
    "password_hash": "sha256:..."
}

// 连接请求
{
    "type": "connect_request",
    "from_device": "abc123",
    "to_device": "xyz789",
    "password": "encrypted:..."
}

// 连接响应
{
    "type": "connect_response",
    "accepted": true,
    "turn_server": "turn:turn.example.com:3478",
    "turn_credential": "temp_token_123"
}

// 连接信息交换
{
    "type": "connection_info",
    "local_ip": "192.168.1.100",
    "local_port": 50000,
    "public_ip": "203.0.113.50",
    "public_port": 12345
}
```

#### 11.1.2 信令客户端

```cpp
// src/network/signaling/signaling_client.h

class SignalingClient {
public:
    using MessageCallback = std::function<void(const SignalingMessage&)>;
    
    Result<void> Connect(const std::string& server_url);
    void Disconnect();
    
    Result<void> Register(const std::string& device_id, 
                          const std::string& password_hash);
    Result<void> RequestConnect(const std::string& target_device_id,
                                const std::string& password);
    Result<void> SendConnectionInfo(const ConnectionInfo& info);
    
    void SetMessageCallback(MessageCallback callback);

private:
    std::unique_ptr<WebSocketClient> ws_client_;
    MessageCallback callback_;
};
```

### 11.2 TURN 客户端

TURN (Traversal Using Relays around NAT) 用于在 NAT 穿透失败时中继流量。

#### 11.2.1 TURN 协议流程

```
客户端 A                    TURN Server                    客户端 B
    |                            |                            |
    |--- Allocate Request ------>|                            |
    |<-- Allocate Response ------|                            |
    |   (relayed-address)        |                            |
    |                            |                            |
    |--- CreatePermission ------>|                            |
    |   (peer B address)         |                            |
    |<-- Success Response -------|                            |
    |                            |                            |
    |--- ChannelBind ----------->|                            |
    |<-- Success Response -------|                            |
    |                            |                            |
    |=== ChannelData (媒体流) ===>|=== Forward to B =========>|
    |<== ChannelData (媒体流) ====|<== Forward from B ========|
```

#### 11.2.2 TURN 客户端实现

```cpp
// src/network/connection/turn_connection.h

class TurnConnection : public IConnection {
public:
    struct Config {
        std::string server_host;
        uint16_t server_port;
        std::string username;
        std::string credential;
    };
    
    Result<void> Connect(const Config& config);
    Result<void> AllocateRelayAddress();
    Result<void> CreatePermission(const Endpoint& peer);
    Result<void> BindChannel(const Endpoint& peer, uint16_t channel_number);
    
    // IConnection 接口
    bool Send(const uint8_t* data, size_t len) override;
    bool Receive(uint8_t* buffer, size_t* len) override;
    
    Endpoint GetRelayedAddress() const;

private:
    UdpSocket socket_;
    Endpoint relay_address_;
    std::map<Endpoint, uint16_t> channel_bindings_;
};
```

### 11.3 连接管理器

统一管理直连和中继连接，实现自动选择最优路径。

```cpp
// src/network/connection/connection_manager.h

class ConnectionManager {
public:
    enum class ConnectionMode {
        Direct,     // 局域网直连
        TurnRelay   // TURN 中继
    };
    
    struct ConnectionConfig {
        Endpoint local_endpoint;
        Endpoint remote_endpoint;
        TurnConnection::Config turn_config;
        bool prefer_direct = true;  // 优先尝试直连
    };
    
    Result<void> Connect(const ConnectionConfig& config);
    void Disconnect();
    
    ConnectionMode GetCurrentMode() const;
    
    bool Send(const uint8_t* data, size_t len);
    bool Receive(uint8_t* buffer, size_t* len);

private:
    Result<bool> TryDirectConnect(const Endpoint& remote);
    Result<void> FallbackToTurn();
    
    std::unique_ptr<DirectConnection> direct_conn_;
    std::unique_ptr<TurnConnection> turn_conn_;
    ConnectionMode current_mode_;
};
```

### 11.4 TURN 服务器部署

推荐使用开源 coturn 服务器：

```bash
# 安装 coturn (Ubuntu/Debian)
sudo apt install coturn

# 配置文件 /etc/turnserver.conf
listening-port=3478
tls-listening-port=5349
realm=zenremote.example.com
server-name=zenremote.example.com

# 静态用户（测试用）
user=zenremote:password123

# 或动态认证（生产环境）
use-auth-secret
static-auth-secret=your-secret-key

# 启动服务
sudo systemctl enable coturn
sudo systemctl start coturn
```

### 11.5 连接流程

```
1. 用户 A 输入用户 B 的设备 ID
2. A 通过信令服务器发送连接请求
3. B 收到请求，用户确认后接受
4. 双方交换连接信息（本地 IP、公网 IP）
5. 尝试直连：
   a. A 尝试连接 B 的公网地址
   b. B 尝试连接 A 的公网地址
   c. 如果任一方向成功，使用直连
6. 直连失败，切换到 TURN：
   a. 双方各自向 TURN 服务器 Allocate
   b. 创建 Permission，绑定 Channel
   c. 通过 TURN 中继通信
```

---

## 12. Phase 7: 性能检测与优化详细设计

### 12.1 Stats 系统设计（参考 WebRTC RTCStats）

#### 12.1.1 统计数据模型

```cpp
// src/core/stats/media_stats.h

// 基础统计类型
struct StatsReport {
    int64_t timestamp_us;  // 统计时间戳
    std::string type;      // 统计类型
    std::string id;        // 唯一标识
};

// 发送端视频统计
struct VideoSendStats : public StatsReport {
    // 编码统计
    uint32_t frames_encoded = 0;        // 已编码帧数
    uint32_t key_frames_encoded = 0;    // 关键帧数
    uint64_t bytes_encoded = 0;         // 编码字节数
    double encode_time_ms = 0;          // 平均编码耗时
    int encoder_cpu_usage = 0;          // 编码器 CPU 占用 %
    
    // 发送统计
    uint64_t packets_sent = 0;          // 已发送包数
    uint64_t bytes_sent = 0;            // 已发送字节数
    uint64_t retransmitted_packets = 0; // 重传包数
    uint32_t nack_count = 0;            // 收到的 NACK 数
    
    // 码率统计
    uint32_t target_bitrate_bps = 0;    // 目标码率
    uint32_t actual_bitrate_bps = 0;    // 实际码率
    
    // 质量统计
    int frame_width = 0;
    int frame_height = 0;
    double frames_per_second = 0;
    std::string encoder_implementation;  // 编码器名称 (nvenc/qsv/x264)
};

// 接收端视频统计
struct VideoRecvStats : public StatsReport {
    // 接收统计
    uint64_t packets_received = 0;      // 已接收包数
    uint64_t bytes_received = 0;        // 已接收字节数
    uint64_t packets_lost = 0;          // 丢包数
    double packet_loss_rate = 0;        // 丢包率
    
    // 解码统计
    uint32_t frames_decoded = 0;        // 已解码帧数
    uint32_t frames_dropped = 0;        // 丢弃帧数
    double decode_time_ms = 0;          // 平均解码耗时
    
    // 渲染统计
    uint32_t frames_rendered = 0;       // 已渲染帧数
    double render_delay_ms = 0;         // 渲染延迟
    
    // JitterBuffer 统计
    double jitter_ms = 0;               // 抖动
    double jitter_buffer_delay_ms = 0;  // JitterBuffer 延迟
    uint32_t jitter_buffer_size = 0;    // JitterBuffer 大小
    
    // 质量统计
    int frame_width = 0;
    int frame_height = 0;
    double frames_per_second = 0;
    std::string decoder_implementation;  // 解码器名称
};

// 网络连接统计
struct NetworkStats : public StatsReport {
    // RTT 统计
    double current_rtt_ms = 0;          // 当前 RTT
    double average_rtt_ms = 0;          // 平均 RTT
    double min_rtt_ms = 0;              // 最小 RTT
    double max_rtt_ms = 0;              // 最大 RTT
    
    // 带宽估计
    uint32_t available_outgoing_bitrate = 0;  // 可用上行带宽
    uint32_t available_incoming_bitrate = 0;  // 可用下行带宽
    
    // 连接状态
    std::string connection_state;       // connected/disconnected
    std::string local_address;          // 本地地址
    std::string remote_address;         // 远程地址
    bool is_relay = false;              // 是否通过 TURN 中继
};

// 端到端延迟统计
struct LatencyStats : public StatsReport {
    double capture_to_encode_ms = 0;    // 采集到编码延迟
    double encode_to_send_ms = 0;       // 编码到发送延迟
    double network_delay_ms = 0;        // 网络传输延迟
    double receive_to_decode_ms = 0;    // 接收到解码延迟
    double decode_to_render_ms = 0;     // 解码到渲染延迟
    double total_delay_ms = 0;          // 端到端总延迟
};
```

#### 12.1.2 Stats 收集器

```cpp
// src/core/stats/stats_collector.h

class StatsCollector {
public:
    // 注册统计源
    void RegisterVideoEncoder(VideoEncoder* encoder);
    void RegisterVideoDecoder(VideoDecoder* decoder);
    void RegisterJitterBuffer(JitterBuffer* jitter_buffer);
    void RegisterPacer(Pacer* pacer);
    void RegisterTransport(Transport* transport);
    
    // 获取统计数据
    VideoSendStats GetVideoSendStats();
    VideoRecvStats GetVideoRecvStats();
    NetworkStats GetNetworkStats();
    LatencyStats GetLatencyStats();
    
    // 获取所有统计（JSON 格式）
    std::string GetStatsJson();
    
    // 统计回调
    using StatsCallback = std::function<void(const std::string& stats_json)>;
    void SetStatsCallback(StatsCallback callback, int interval_ms = 1000);

private:
    void CollectStats();
    void CalculateBitrate();
    void CalculateFrameRate();
    
    std::mutex mutex_;
    std::unique_ptr<loki::RepeatingTaskHandle> stats_task_;
};
```

### 12.2 自适应码率控制 (ABR)

```cpp
// src/core/stats/bitrate_controller.h

class BitrateController {
public:
    struct Config {
        uint32_t min_bitrate_bps = 500000;    // 最低 500 Kbps
        uint32_t max_bitrate_bps = 8000000;   // 最高 8 Mbps
        uint32_t start_bitrate_bps = 2000000; // 起始 2 Mbps
        double rtt_threshold_ms = 150;        // RTT 阈值
        double loss_threshold = 0.05;         // 丢包率阈值 5%
    };
    
    void SetConfig(const Config& config);
    void OnRttUpdate(double rtt_ms);
    void OnPacketLoss(double loss_rate);
    void OnBandwidthEstimate(uint32_t bandwidth_bps);
    
    uint32_t GetTargetBitrate() const;
    
    // 码率变化回调
    using BitrateCallback = std::function<void(uint32_t new_bitrate)>;
    void SetBitrateCallback(BitrateCallback callback);

private:
    void AdjustBitrate();
    
    Config config_;
    uint32_t current_bitrate_bps_;
    double smoothed_rtt_ms_ = 0;
    double smoothed_loss_rate_ = 0;
};
```

### 12.3 端到端延迟测量

```cpp
// 使用 NTP 时间戳测量端到端延迟

// 发送端：在视频帧中嵌入采集时间戳
struct FrameTimestamp {
    int64_t capture_time_us;    // 采集时间
    int64_t encode_start_us;    // 开始编码时间
    int64_t encode_end_us;      // 编码完成时间
    int64_t send_time_us;       // 发送时间
};

// 接收端：记录各阶段时间戳
struct FrameLatencyInfo {
    FrameTimestamp sender_ts;   // 发送端时间戳（通过 RTP 扩展头传输）
    int64_t receive_time_us;    // 接收时间
    int64_t decode_start_us;    // 开始解码时间
    int64_t decode_end_us;      // 解码完成时间
    int64_t render_time_us;     // 渲染时间
    
    double GetTotalLatencyMs() const {
        return (render_time_us - sender_ts.capture_time_us) / 1000.0;
    }
};
```

### 12.4 性能诊断 UI

```cpp
// src/app/ui/stats_overlay.h

class StatsOverlay : public QWidget {
public:
    void UpdateStats(const std::string& stats_json);
    void SetVisible(bool visible);

    // 显示内容:
    // ┌─────────────────────────────┐
    // │ 📊 性能统计                  │
    // │ ─────────────────────────── │
    // │ 分辨率: 1920x1080 @ 30fps   │
    // │ 编码器: h264_nvenc          │
    // │ 码率: 2.1 Mbps              │
    // │ ─────────────────────────── │
    // │ RTT: 15ms                   │
    // │ 丢包率: 0.1%                │
    // │ 抖动: 5ms                   │
    // │ ─────────────────────────── │
    // │ 端到端延迟: 45ms            │
    // │   采集→编码: 8ms            │
    // │   网络传输: 15ms            │
    // │   解码→渲染: 22ms           │
    // └─────────────────────────────┘
};
```

### 12.5 Stats 日志与导出

```cpp
// src/core/stats/stats_logger.h

class StatsLogger {
public:
    void Start(const std::string& log_file);
    void Stop();
    void LogStats(const std::string& stats_json);
    
    // 导出为 CSV（便于分析）
    void ExportToCsv(const std::string& csv_file);
    
    // 导出为 Chrome Tracing 格式（可用 chrome://tracing 分析）
    void ExportToTrace(const std::string& trace_file);
};
```

### 12.6 文件结构

```
src/core/stats/
├── media_stats.h            # 统计数据模型定义
├── stats_collector.h/cpp    # 统计数据收集器
├── bitrate_controller.h/cpp # 自适应码率控制
├── stats_logger.h/cpp       # 统计日志记录
└── latency_tracker.h/cpp    # 端到端延迟追踪

src/app/ui/
└── stats_overlay.h/cpp      # 性能统计 UI 覆盖层
```

---

## 13. 下一步行动

1. **立即开始**：从 zenplay 迁移解码器和渲染器代码
2. **Week 1 目标**：编解码器可独立测试（不依赖网络层）
3. **验收标准**：

### Phase 1-4 (Windows) 验收标准
- [ ] 屏幕采集帧 → H.264 编码成功
- [ ] H.264 数据 → 解码 → 渲染显示
- [ ] 硬件加速路径可用时自动启用
- [ ] 远程鼠标/键盘控制正常
- [ ] 单机双实例测试通过

### Phase 5 (macOS) 验收标准
- [ ] SCStreamCaptureKit 屏幕采集正常
- [ ] VideoToolbox 硬编/硬解正常
- [ ] CGEvent 输入注入正常
- [ ] Windows ↔ macOS 双向控制正常

### Phase 6 (公网) 验收标准
- [ ] 信令服务器设备注册/发现正常
- [ ] 同局域网自动直连
- [ ] 跨局域网自动切换 TURN 中继
- [ ] 中继模式下延迟 < 200ms

### Phase 7 (性能检测) 验收标准
- [ ] Stats API 能正确返回所有统计数据
- [ ] 实时 FPS/码率/RTT/丢包率统计准确
- [ ] 端到端延迟测量误差 < 5ms
- [ ] 自适应码率能根据网络状况动态调整
- [ ] Stats UI 能实时显示性能数据
- [ ] 支持 Stats 日志导出与分析

---

## 附录 A：zenplay 代码迁移清单

### A.1 需要迁移的文件

```
# 解码器
src/player/codec/decode.h          → src/media/codec/decoder.h
src/player/codec/decode.cpp        → src/media/codec/decoder.cpp
src/player/codec/video_decoder.h   → src/media/codec/video_decoder.h
src/player/codec/video_decoder.cpp → src/media/codec/video_decoder.cpp
src/player/codec/hw_decoder_context.h   → src/media/codec/hw_decoder_context.h
src/player/codec/hw_decoder_context.cpp → src/media/codec/hw_decoder_context.cpp
src/player/codec/hw_decoder_type.h      → src/media/codec/hw_decoder_type.h
src/player/codec/hw_decoder_type.cpp    → src/media/codec/hw_decoder_type.cpp

# 渲染器
src/player/video/render/renderer.h          → src/media/renderer/renderer.h
src/player/video/render/impl/sdl/sdl_renderer.h   → src/media/renderer/sdl_renderer.h
src/player/video/render/impl/sdl/sdl_renderer.cpp → src/media/renderer/sdl_renderer.cpp
src/player/video/render/impl/sdl/sdl_manager.h    → src/media/renderer/sdl_manager.h
src/player/video/render/impl/sdl/sdl_manager.cpp  → src/media/renderer/sdl_manager.cpp
src/player/video/render/impl/d3d11/d3d11_renderer.h   → src/media/renderer/d3d11_renderer.h
src/player/video/render/impl/d3d11/d3d11_renderer.cpp → src/media/renderer/d3d11_renderer.cpp
# D3D11 其他文件类似...
```

### A.2 迁移时需要修改的内容

1. **命名空间**: `zenplay` → `zenremote`
2. **日志宏**: `MODULE_INFO` → `ZENREMOTE_INFO` 或直接使用 spdlog
3. **Include 路径**: 适配 zenremote 目录结构
4. **Result 类型**: 确认与 zenremote 的 Result<T> 兼容
5. **删除播放器专有逻辑**: Seek、暂停、倍速等

---

## 附录 B：跨平台抽象层设计

为支持 Windows 和 macOS，需要设计平台抽象接口：

```cpp
// src/media/capture/screen_capturer.h
class IScreenCapturer {
public:
    virtual ~IScreenCapturer() = default;
    virtual Result<void> Start(const CaptureConfig& config) = 0;
    virtual void Stop() = 0;
    virtual AVFrame* CaptureFrame() = 0;
};

// 工厂函数
std::unique_ptr<IScreenCapturer> CreateScreenCapturer();

// src/media/codec/video_encoder.h  
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual Result<void> Open(const EncoderConfig& config) = 0;
    virtual void Close() = 0;
    virtual Result<std::vector<AVPacketPtr>> Encode(AVFrame* frame) = 0;
};

std::unique_ptr<IVideoEncoder> CreateVideoEncoder();

// src/app/control/input_injector.h
class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual void InjectMouseMove(int x, int y) = 0;
    virtual void InjectMouseClick(int button, bool down) = 0;
    virtual void InjectKeyEvent(uint32_t keyCode, bool down) = 0;
};

std::unique_ptr<IInputInjector> CreateInputInjector();
```

### B.1 平台检测宏

```cpp
// src/common/platform.h
#if defined(_WIN32)
    #define ZENREMOTE_WINDOWS 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define ZENREMOTE_MACOS 1
    #endif
#elif defined(__linux__)
    #define ZENREMOTE_LINUX 1
#endif
```

---

## 附录 C：新增依赖

### C.1 Phase 5 (macOS) 依赖

| 依赖 | 用途 | 来源 |
|------|------|------|
| ScreenCaptureKit | 屏幕采集 | macOS SDK (12.3+) |
| VideoToolbox | 硬件编解码 | macOS SDK |
| CoreGraphics | 输入注入 | macOS SDK |

### C.2 Phase 6 (公网) 依赖

| 依赖 | 用途 | 来源 |
|------|------|------|
| libwebsockets | WebSocket 客户端 | Conan |
| OpenSSL | TLS/DTLS 加密 | Conan (可选) |

```python
# conanfile.py 更新
def requirements(self):
    # 现有依赖...
    self.requires("libwebsockets/4.3.2")  # Phase 6 新增
    self.requires("openssl/3.1.0")        # Phase 6 新增 (可选)
```

---

**文档版本**: v1.2  
**创建日期**: 2025-12-28  
**更新日期**: 2025-12-28  
**更新内容**: 
- v1.1: 将 control 模块移至 app 层目录
- v1.1: 新增 Phase 5 macOS 平台支持
- v1.1: 新增 Phase 6 公网 TURN 服务器支持
- v1.2: 新增 Phase 7 性能检测与优化系统
**作者**: GitHub Copilot  
