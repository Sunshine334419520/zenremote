# WebRTC 架构分析与重构建议

## 一、WebRTC 架构分析

### 1.1 WebRTC 的核心分层

```
┌─────────────────────────────────────────────────────┐
│  Application Layer (应用层)                         │
│  - 应用逻辑：视频会议、屏幕共享、文件传输等         │
│  - 直接使用 PeerConnection API                      │
└────────────────┬───────────────────────────────────┘
                 │
┌────────────────┴───────────────────────────────────┐
│  PeerConnection (对等连接层)                       │
│  - 会话协商 (SDP Offer/Answer)                     │
│  - ICE 候选地址收集                                │
│  - 连接状态管理                                     │
│  - Track 管理 (addTrack, addTransceiver)           │
└────────────────┬───────────────────────────────────┘
                 │
     ┌───────────┴───────────┐
     │                       │
┌────▼──────────┐   ┌────────▼──────────┐
│  RTP Sender   │   │  RTP Receiver     │
│  (媒体发送)   │   │  (媒体接收)       │
└────┬──────────┘   └────────┬──────────┘
     │                       │
┌────▼───────────────────────▼──────────┐
│  DTLS-SRTP (加密传输层)               │
│  - DTLS 握手                          │
│  - SRTP 加密                          │
└────────────────┬──────────────────────┘
                 │
┌────────────────┴──────────────────────┐
│  ICE Transport (连接层)               │
│  - STUN/TURN 处理                     │
│  - 候选地址对选择                     │
│  - 连接切换                           │
└────────────────┬──────────────────────┘
                 │
┌────────────────┴──────────────────────┐
│  UDP/TCP Socket (网络 I/O 层)        │
└───────────────────────────────────────┘
```

### 1.2 WebRTC 的关键设计理念

#### 设计理念 1: PeerConnection 是核心抽象

```javascript
// WebRTC 应用层代码示例
const pc = new RTCPeerConnection(config);

// 添加媒体轨道（应用层决定发送什么）
const videoTrack = localStream.getVideoTracks()[0];
pc.addTrack(videoTrack, localStream);

// 创建 Data Channel（应用层决定发送数据）
const dataChannel = pc.createDataChannel("input");
dataChannel.send(JSON.stringify({type: "mouse", x: 100, y: 200}));

// 接收媒体轨道（应用层决定如何处理）
pc.ontrack = (event) => {
  const remoteVideo = document.getElementById('remote');
  remoteVideo.srcObject = event.streams[0];
};
```

**关键点**：
- ✅ PeerConnection **不关心**发送的是视频还是音频还是数据
- ✅ PeerConnection **只负责**连接管理和传输
- ✅ 应用层通过 **Track/DataChannel** 抽象发送任何内容

#### 设计理念 2: Track 抽象（媒体轨道）

```
Track = 一个独立的媒体流
  - VideoTrack: 视频轨道
  - AudioTrack: 音频轨道
  - 每个 Track 独立编码、发送、接收

PeerConnection 管理多个 Track:
  pc.addTrack(videoTrack);
  pc.addTrack(audioTrack);
  
  // 内部自动：
  // - 为每个 Track 分配 SSRC
  // - 创建 RTP Sender
  // - 打包发送
```

#### 设计理念 3: DataChannel 抽象（数据通道）

```javascript
// 创建可靠的数据通道
const channel = pc.createDataChannel("control", {
  ordered: true,      // 有序
  maxRetransmits: 3   // 最多重传 3 次
});

// 应用层决定发送什么
channel.send(JSON.stringify({
  type: "mouse",
  x: 100,
  y: 200
}));

// 接收处理
channel.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === "mouse") {
    moveMouse(data.x, data.y);
  }
};
```

**关键点**：
- ✅ DataChannel **不关心**发送的是鼠标还是键盘
- ✅ 应用层**自己序列化**数据（JSON/Protobuf/等）
- ✅ PeerConnection 只负责**可靠传输**

#### 设计理念 4: 分层职责清晰

| 层次 | 职责 | 不负责 |
|------|------|--------|
| **Application** | 业务逻辑、UI、编解码 | 网络传输 |
| **PeerConnection** | 连接管理、Track 管理 | 具体发送什么内容 |
| **RTP Sender/Receiver** | RTP 打包、序列号管理 | 知道发送的是视频还是音频 |
| **ICE Transport** | 连接建立、NAT 穿透 | 媒体数据处理 |
| **Socket** | UDP/TCP I/O | 协议细节 |

---

## 二、当前设计的问题

### 2.1 ConnectionManager 职责过重

```cpp
// ❌ 当前设计：ConnectionManager 知道太多细节
class ConnectionManager {
  // 连接管理 ✅
  Connect();
  Disconnect();
  
  // 但是还要知道具体发送什么？ ❌
  SendVideoFrame(...);      // 为什么要知道是视频？
  SendAudioPacket(...);     // 为什么要知道是音频？
  SendMouseMove(...);       // 为什么要知道是鼠标？
  SendKeyEvent(...);        // 为什么要知道是键盘？
};
```

**问题**：
1. ❌ 职责不清：连接管理器为什么要知道发送的是鼠标还是键盘？
2. ❌ 扩展性差：添加新的输入类型（触屏、手柄）需要修改 ConnectionManager
3. ❌ 层次混乱：应用层逻辑（视频/音频/输入）混入连接层

### 2.2 缺少 Track 抽象

```cpp
// ❌ 当前设计：没有 Track 概念
session.SendVideoFrame(...);  // 直接发送
session.SendAudioPacket(...);

// ✅ WebRTC 设计：通过 Track 抽象
VideoTrack videoTrack = ...;
pc.addTrack(videoTrack);
videoTrack.send(frame);  // Track 负责编码和发送
```

**问题**：
- ❌ 没有独立的媒体流抽象
- ❌ 无法灵活管理多个媒体流
- ❌ 无法动态添加/移除媒体流

### 2.3 缺少 DataChannel 抽象

```cpp
// ❌ 当前设计：硬编码输入类型
session.SendMouseMove(...);
session.SendKeyEvent(...);

// ✅ WebRTC 设计：通用数据通道
DataChannel inputChannel = pc.createDataChannel("input");
inputChannel.send(mouseEvent);  // 应用层决定发送什么
inputChannel.send(keyEvent);
```

**问题**：
- ❌ 每种输入类型都要添加新方法
- ❌ 无法灵活扩展（触屏、手柄、自定义事件）
- ❌ 可靠性机制硬编码，无法配置

---

## 三、重构建议

### 3.1 新的架构设计

```
┌─────────────────────────────────────────────────────┐
│  Application Layer (应用层)                         │
│  - ControllerSession / ControlledSession            │
│  - 管理 Track 和 DataChannel                        │
│  - 决定发送什么内容                                 │
└────────────────┬───────────────────────────────────┘
                 │ 使用
┌────────────────▼───────────────────────────────────┐
│  PeerConnection (对等连接层)                       │
│  - 连接管理 (Connect/Disconnect)                   │
│  - Track 管理 (AddTrack/RemoveTrack)               │
│  - DataChannel 管理 (CreateDataChannel)            │
│  - 不知道具体发送什么内容 ✅                        │
└────────────────┬───────────────────────────────────┘
                 │
     ┌───────────┴────────────┐
     │                        │
┌────▼──────────┐   ┌─────────▼─────────┐
│  MediaTrack   │   │  DataChannel      │
│  (媒体轨道)   │   │  (数据通道)       │
│  - VideoTrack │   │  - Reliable       │
│  - AudioTrack │   │  - Unreliable     │
└────┬──────────┘   └─────────┬─────────┘
     │                        │
┌────▼────────────────────────▼─────────┐
│  Transport Layer (传输层)             │
│  - RTPSender / RTPReceiver            │
│  - ReliableTransport / UnreliableTransport │
└────────────────┬──────────────────────┘
                 │
┌────────────────▼──────────────────────┐
│  Connection Layer (连接层)            │
│  - DirectConnection / TurnConnection  │
└────────────────┬──────────────────────┘
                 │
┌────────────────▼──────────────────────┐
│  I/O Layer (网络 I/O)                 │
│  - UdpSocket                          │
└───────────────────────────────────────┘
```

### 3.2 核心接口设计

#### 接口 1: PeerConnection（对等连接）

```cpp
/**
 * @brief 对等连接 - 管理连接和传输通道
 * 
 * 参考 WebRTC RTCPeerConnection
 * 职责：
 * - 连接生命周期管理
 * - Track 管理（添加/移除媒体轨道）
 * - DataChannel 管理（创建数据通道）
 * 
 * 不负责：
 * - 不知道发送的是视频还是音频（由 Track 决定）
 * - 不知道发送的是鼠标还是键盘（由 DataChannel + 应用层决定）
 * - 不处理编解码（由 Track 决定）
 */
class PeerConnection {
 public:
  struct Config {
    ConnectionMode mode;  // Direct / TURN / Auto
    std::string remote_ip;
    uint16_t remote_port;
    // ... TURN 配置
  };

  // 连接管理
  Result<void> Initialize(const Config& config);
  Result<void> Connect();
  void Disconnect();
  bool IsConnected() const;

  // Track 管理（媒体轨道）
  Result<void> AddTrack(std::shared_ptr<MediaTrack> track);
  Result<void> RemoveTrack(const std::string& track_id);
  std::vector<std::shared_ptr<MediaTrack>> GetTracks() const;

  // DataChannel 管理（数据通道）
  Result<std::shared_ptr<DataChannel>> CreateDataChannel(
    const std::string& label,
    const DataChannelConfig& config
  );

  // 事件回调
  void SetOnTrackCallback(std::function<void(std::shared_ptr<MediaTrack>)> cb);
  void SetOnDataChannelCallback(std::function<void(std::shared_ptr<DataChannel>)> cb);

 private:
  std::unique_ptr<BaseConnection> connection_;
  std::vector<std::shared_ptr<MediaTrack>> tracks_;
  std::vector<std::shared_ptr<DataChannel>> channels_;
};
```

#### 接口 2: MediaTrack（媒体轨道）

```cpp
/**
 * @brief 媒体轨道抽象 - 独立的媒体流
 * 
 * 参考 WebRTC MediaStreamTrack
 * 职责：
 * - 管理单个媒体流（视频或音频）
 * - 编码参数配置
 * - 帧发送和接收
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

  // 发送侧接口
  virtual Result<void> SendFrame(const uint8_t* data, size_t length,
                                  uint32_t timestamp) = 0;

  // 接收侧回调
  virtual void SetOnFrameCallback(
    std::function<void(const uint8_t*, size_t, uint32_t)> cb) = 0;
};

/**
 * @brief 视频轨道
 */
class VideoTrack : public MediaTrack {
 public:
  struct Config {
    std::string id;
    std::string codec = "H264";  // H264 / VP8 / VP9
    uint32_t bitrate_bps = 2500000;
    uint32_t framerate = 30;
  };

  explicit VideoTrack(const Config& config);

  Kind GetKind() const override { return Kind::kVideo; }

  // 发送 H.264 帧
  Result<void> SendFrame(const uint8_t* data, size_t length,
                         uint32_t timestamp_90khz) override;
};

/**
 * @brief 音频轨道
 */
class AudioTrack : public MediaTrack {
 public:
  struct Config {
    std::string id;
    std::string codec = "Opus";
    uint32_t sample_rate = 48000;
  };

  explicit AudioTrack(const Config& config);

  Kind GetKind() const override { return Kind::kAudio; }

  // 发送 Opus 包
  Result<void> SendFrame(const uint8_t* data, size_t length,
                         uint32_t timestamp_48khz) override;
};
```

#### 接口 3: DataChannel（数据通道）

```cpp
/**
 * @brief 数据通道 - 通用数据传输
 * 
 * 参考 WebRTC RTCDataChannel
 * 职责：
 * - 发送任意二进制数据
 * - 可配置可靠性（有序/无序，重传次数）
 * - 不关心数据内容（由应用层决定）
 */
class DataChannel {
 public:
  struct Config {
    bool ordered = true;        // 是否有序
    int max_retransmits = 3;    // 最大重传次数（-1 = 无限）
    int max_packet_life_time = 0;  // 包最大生存时间（ms，0 = 禁用）
  };

  enum class State {
    kConnecting,
    kOpen,
    kClosing,
    kClosed,
  };

  virtual ~DataChannel() = default;

  // 基本信息
  virtual std::string GetLabel() const = 0;
  virtual State GetState() const = 0;

  // 发送数据（应用层决定数据格式）
  virtual Result<void> Send(const uint8_t* data, size_t length) = 0;
  virtual Result<void> Send(const std::string& text) = 0;

  // 接收回调
  virtual void SetOnMessageCallback(
    std::function<void(const uint8_t*, size_t)> cb) = 0;

  // 状态回调
  virtual void SetOnOpenCallback(std::function<void()> cb) = 0;
  virtual void SetOnCloseCallback(std::function<void()> cb) = 0;
};
```

### 3.3 应用层使用示例

#### 示例 1: 控制端（发送视频）

```cpp
class ControllerSession {
 public:
  Result<void> Initialize(const std::string& remote_ip) {
    // 1. 创建 PeerConnection
    PeerConnection::Config config;
    config.mode = ConnectionMode::kDirect;
    config.remote_ip = remote_ip;
    config.remote_port = 50000;

    pc_ = std::make_unique<PeerConnection>();
    auto result = pc_->Initialize(config);
    if (result.IsErr()) return result;

    // 2. 创建视频轨道
    VideoTrack::Config video_config;
    video_config.id = "video0";
    video_config.codec = "H264";
    video_config.bitrate_bps = 2500000;

    video_track_ = std::make_shared<VideoTrack>(video_config);
    pc_->AddTrack(video_track_);

    // 3. 创建音频轨道
    AudioTrack::Config audio_config;
    audio_config.id = "audio0";
    audio_track_ = std::make_shared<AudioTrack>(audio_config);
    pc_->AddTrack(audio_track_);

    // 4. 创建输入数据通道（可靠）
    DataChannel::Config channel_config;
    channel_config.ordered = true;
    channel_config.max_retransmits = 3;

    auto channel_result = pc_->CreateDataChannel("input", channel_config);
    if (channel_result.IsOk()) {
      input_channel_ = channel_result.Value();
      
      // 设置接收回调
      input_channel_->SetOnMessageCallback(
        [this](const uint8_t* data, size_t len) {
          OnInputEventReceived(data, len);
        }
      );
    }

    // 5. 建立连接
    return pc_->Connect();
  }

  // 发送视频帧 - 通过 VideoTrack
  void SendVideoFrame(const VideoFrame& frame) {
    if (video_track_ && video_track_->IsEnabled()) {
      video_track_->SendFrame(
        frame.data, frame.size,
        GetTimestamp90kHz()
      );
    }
  }

  // 发送音频 - 通过 AudioTrack
  void SendAudioPacket(const AudioPacket& packet) {
    if (audio_track_ && audio_track_->IsEnabled()) {
      audio_track_->SendFrame(
        packet.data, packet.size,
        GetTimestamp48kHz()
      );
    }
  }

 private:
  void OnInputEventReceived(const uint8_t* data, size_t len) {
    // 应用层解析输入事件（JSON/Protobuf/自定义格式）
    InputEvent event = ParseInputEvent(data, len);
    
    switch (event.type) {
      case InputEventType::kMouseMove:
        platform::MoveMouse(event.x, event.y);
        break;
      case InputEventType::kKeyDown:
        platform::SimulateKeyDown(event.key_code);
        break;
      // ...
    }
  }

  std::unique_ptr<PeerConnection> pc_;
  std::shared_ptr<VideoTrack> video_track_;
  std::shared_ptr<AudioTrack> audio_track_;
  std::shared_ptr<DataChannel> input_channel_;
};
```

#### 示例 2: 被控端（接收视频，发送输入）

```cpp
class ControlledSession {
 public:
  Result<void> Initialize(uint16_t local_port) {
    // 1. 创建 PeerConnection
    PeerConnection::Config config;
    config.mode = ConnectionMode::kDirect;
    config.local_port = local_port;

    pc_ = std::make_unique<PeerConnection>();
    auto result = pc_->Initialize(config);
    if (result.IsErr()) return result;

    // 2. 设置接收回调
    pc_->SetOnTrackCallback([this](std::shared_ptr<MediaTrack> track) {
      OnRemoteTrackAdded(track);
    });

    pc_->SetOnDataChannelCallback([this](std::shared_ptr<DataChannel> channel) {
      OnRemoteDataChannel(channel);
    });

    // 3. 建立连接
    return pc_->Connect();
  }

  // 本地输入事件 - 通过 DataChannel 发送
  void OnLocalMouseMove(int x, int y) {
    if (input_channel_ && input_channel_->GetState() == DataChannel::State::kOpen) {
      // 应用层决定序列化格式
      InputEvent event;
      event.type = InputEventType::kMouseMove;
      event.x = x;
      event.y = y;

      std::vector<uint8_t> data = SerializeInputEvent(event);
      input_channel_->Send(data.data(), data.size());
    }
  }

  void OnLocalKeyPress(uint32_t key_code, bool is_down) {
    if (input_channel_) {
      InputEvent event;
      event.type = is_down ? InputEventType::kKeyDown : InputEventType::kKeyUp;
      event.key_code = key_code;

      std::vector<uint8_t> data = SerializeInputEvent(event);
      input_channel_->Send(data.data(), data.size());
    }
  }

 private:
  void OnRemoteTrackAdded(std::shared_ptr<MediaTrack> track) {
    if (track->GetKind() == MediaTrack::Kind::kVideo) {
      // 设置视频帧回调
      track->SetOnFrameCallback(
        [this](const uint8_t* data, size_t len, uint32_t ts) {
          OnVideoFrameReceived(data, len, ts);
        }
      );
    } else if (track->GetKind() == MediaTrack::Kind::kAudio) {
      // 设置音频回调
      track->SetOnFrameCallback(
        [this](const uint8_t* data, size_t len, uint32_t ts) {
          OnAudioPacketReceived(data, len, ts);
        }
      );
    }
  }

  void OnRemoteDataChannel(std::shared_ptr<DataChannel> channel) {
    if (channel->GetLabel() == "input") {
      input_channel_ = channel;
      // 通道已在对端创建，这里只接收
    }
  }

  void OnVideoFrameReceived(const uint8_t* data, size_t len, uint32_t ts) {
    // 解码渲染
    VideoFrame frame = video_decoder_->Decode(data, len);
    video_renderer_->Render(frame);
  }

  void OnAudioPacketReceived(const uint8_t* data, size_t len, uint32_t ts) {
    // 解码播放
    AudioSamples samples = audio_decoder_->Decode(data, len);
    audio_player_->Play(samples);
  }

  std::unique_ptr<PeerConnection> pc_;
  std::shared_ptr<DataChannel> input_channel_;
};
```

---

## 四、重构对比

### 4.1 职责划分对比

| 组件 | 当前设计 ❌ | 重构后 ✅ |
|------|-----------|----------|
| **ConnectionManager** | 连接 + 媒体 + 输入 | → PeerConnection：只管连接 |
| **RTPSender** | 在 ConnectionManager 内部 | → MediaTrack 内部使用 |
| **ReliableInput** | 在 ConnectionManager 内部 | → DataChannel（通用） |
| **应用层** | 调用 SendVideoFrame 等 | → 管理 Track 和 DataChannel |

### 4.2 代码对比

#### 发送视频

```cpp
// ❌ 当前设计
session.SendVideoFrame(data, size, timestamp);  // 硬编码

// ✅ 重构后
video_track->SendFrame(data, size, timestamp);  // 通过 Track 抽象
```

#### 发送输入

```cpp
// ❌ 当前设计
session.SendMouseMove(x, y);      // 每种输入一个方法
session.SendKeyEvent(key, down);

// ✅ 重构后
InputEvent event = {type: kMouseMove, x, y};
std::vector<uint8_t> data = Serialize(event);
input_channel->Send(data);  // 通用数据通道，应用层决定格式
```

#### 扩展性

```cpp
// ❌ 当前设计：添加触屏支持
// 需要修改 ConnectionManager：
session.SendTouchEvent(...);  // 新增方法

// ✅ 重构后：添加触屏支持
// 无需修改 PeerConnection/DataChannel：
TouchEvent event = {...};
std::vector<uint8_t> data = Serialize(event);
input_channel->Send(data);  // 复用现有通道
```

### 4.3 灵活性对比

| 场景 | 当前设计 | 重构后 |
|------|---------|--------|
| **禁用音频** | 需要在应用层不调用 SendAudioPacket | `audio_track->SetEnabled(false)` |
| **动态添加音频** | 无法实现 | `pc->AddTrack(audio_track)` |
| **自定义数据** | 需要新增 SendCustomData 方法 | 复用 DataChannel |
| **多路视频** | 无法实现 | 添加多个 VideoTrack |

---

## 五、实现计划

### 5.1 Phase 1: 核心抽象

1. **创建 PeerConnection 类**
   - 基本连接管理
   - Track 和 DataChannel 容器
   - 事件回调机制

2. **创建 MediaTrack 抽象**
   - MediaTrack 基类
   - VideoTrack 实现
   - AudioTrack 实现

3. **创建 DataChannel 抽象**
   - DataChannel 接口
   - ReliableDataChannel 实现（基于现有 ReliableInput）
   - UnreliableDataChannel 实现

### 5.2 Phase 2: 重构应用层

1. **重构 ControllerSession**
   - 使用 PeerConnection
   - 管理 VideoTrack/AudioTrack
   - 管理 DataChannel

2. **重构 ControlledSession**
   - 接收 Track 回调
   - 处理 DataChannel 消息

### 5.3 Phase 3: 清理旧代码

1. **删除 ConnectionManager 中的媒体方法**
   - 移除 SendVideoFrame
   - 移除 SendAudioPacket
   - 移除 SendMouseMove 等

2. **保留底层组件**
   - UdpSocket 保持不变
   - DirectConnection 保持不变
   - RTPSender/Receiver 由 Track 内部使用

---

## 六、总结

### 6.1 WebRTC 给我们的启示

1. ✅ **PeerConnection 只管连接，不管内容**
   - 不知道发送的是视频还是音频
   - 不知道发送的是鼠标还是键盘

2. ✅ **Track 抽象管理媒体流**
   - 每个 Track 独立编码、发送、接收
   - 可动态添加/移除

3. ✅ **DataChannel 抽象管理数据流**
   - 通用二进制数据传输
   - 应用层决定数据格式和语义

4. ✅ **分层职责清晰**
   - 应用层：业务逻辑
   - PeerConnection：连接管理
   - Track/DataChannel：传输抽象
   - RTP/Transport：底层传输

### 6.2 重构后的优势

| 优势 | 说明 |
|------|------|
| **职责清晰** | 每层只做自己的事，不越界 |
| **易扩展** | 添加新功能无需修改核心类 |
| **符合标准** | 参考 WebRTC，便于未来互操作 |
| **灵活性高** | Track/DataChannel 可动态管理 |
| **代码简洁** | 应用层代码更清晰易懂 |

### 6.3 下一步行动

1. **审阅设计文档**（本文档）
2. **开始实现 Phase 1**：创建核心抽象
3. **逐步迁移应用层**：使用新接口
4. **清理旧代码**：移除过度封装

**这才是正确的、工业级的、可扩展的架构设计！** 🎯
