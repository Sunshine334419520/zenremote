# 架构重构对比说明

## 一、目录结构对比

### 旧架构
```
network/
├── io/
│   └── udp_socket.h/cpp
├── transport/
│   ├── base_connection.h
│   └── direct_connection.h/cpp
├── connection_manager/
│   └── connection_manager.h/cpp        # ❌ 职责过重
├── protocol/
│   ├── rtp_sender.h/cpp
│   ├── rtp_receiver.h/cpp
│   └── reliable_input.h/cpp
└── examples/
    └── connection_example.cpp
```

### 新架构（WebRTC 风格）
```
network/
├── core/                               # ✅ 核心抽象层
│   ├── peer_connection.h/cpp          # 对等连接（只管连接）
│   ├── media_track.h                  # 媒体轨道接口
│   ├── video_track.h/cpp              # 视频轨道实现
│   ├── audio_track.h/cpp              # 音频轨道实现
│   ├── data_channel.h                 # 数据通道接口
│   └── reliable_data_channel.h/cpp    # 可靠数据通道实现
│
├── transport/                          # 传输层（内部使用）
│   ├── rtp_sender.h/cpp
│   ├── rtp_receiver.h/cpp
│   ├── rtp_packet.h
│   └── reliable_transport.h/cpp
│
├── connection/                         # 连接层（内部使用）
│   ├── base_connection.h
│   └── direct_connection.h/cpp
│
├── io/                                # 网络 I/O
│   └── udp_socket.h/cpp
│
└── session/                           # ✅ 应用层会话
    ├── controller_session.h/cpp       # 控制端会话
    └── controlled_session.h/cpp       # 被控端会话
```

## 二、核心概念对比

### 旧架构：ConnectionManager 职责过重

```cpp
// ❌ ConnectionManager 知道太多细节
class ConnectionManager {
  // 连接管理
  Connect();
  Disconnect();
  
  // ❌ 还要知道发送什么内容
  SendVideoFrame(...);      // 为什么要知道是视频？
  SendAudioPacket(...);     // 为什么要知道是音频？
  SendMouseMove(...);       // 为什么要知道是鼠标？
  SendKeyEvent(...);        // 为什么要知道是键盘？
  
  // ❌ 内部管理所有协议
  private:
    RTPSender* rtp_sender_;
    ReliableInputSender* input_sender_;
};

// 应用层使用
ConnectionManager session;
session.SendVideoFrame(data, len, ts);  // 硬编码
session.SendMouseMove(x, y);            // 每种输入一个方法
```

### 新架构：PeerConnection + Track + DataChannel

```cpp
// ✅ PeerConnection 只管连接，不管内容
class PeerConnection {
  // 连接管理
  Connect();
  Disconnect();
  
  // ✅ 管理抽象的 Track 和 DataChannel
  AddTrack(MediaTrack* track);
  CreateDataChannel(label, config);
  
  // ✅ 不知道发送什么内容
};

// ✅ Track 负责媒体流
class VideoTrack : public MediaTrack {
  SendFrame(data, len, ts);  // 应用层通过 Track 发送
};

// ✅ DataChannel 负责数据流
class DataChannel {
  Send(data, len);  // 应用层决定数据格式
};

// 应用层使用
PeerConnection pc;
auto video_track = std::make_shared<VideoTrack>();
pc.AddTrack(video_track);

auto input_channel = pc.CreateDataChannel("input");

// 发送
video_track->SendFrame(data, len, ts);     // 通过 Track
input_channel->Send(input_data, len);      // 通过 DataChannel
```

## 三、职责划分对比

| 组件 | 旧架构 | 新架构 | 改进 |
|------|--------|--------|------|
| **连接管理** | ConnectionManager（职责混乱） | PeerConnection（职责清晰） | ✅ 只管连接 |
| **媒体发送** | ConnectionManager.SendVideoFrame() | VideoTrack.SendFrame() | ✅ 通过 Track 抽象 |
| **数据发送** | ConnectionManager.SendMouseMove() | DataChannel.Send() | ✅ 通用数据通道 |
| **RTP 层** | 在 ConnectionManager 内部 | 在 Track 内部 | ✅ 封装更好 |
| **应用层** | 直接调用 ConnectionManager | ControllerSession 管理 Track | ✅ 更清晰 |

## 四、代码对比

### 发送视频

#### 旧架构
```cpp
// ❌ 应用层直接调用 ConnectionManager
session.SendVideoFrame(data, size, timestamp);
```

#### 新架构
```cpp
// ✅ 通过 VideoTrack 抽象
video_track->SendFrame(data, size, timestamp);

// 或者通过 Session 封装
controller_session.SendVideoFrame(data, size, timestamp);
```

### 发送输入事件

#### 旧架构
```cpp
// ❌ 每种输入一个方法，硬编码
session.SendMouseMove(x, y);
session.SendMouseClick(button, down, x, y);
session.SendKeyEvent(key, down, mods);

// ❌ 添加触屏支持需要修改 ConnectionManager
session.SendTouchEvent(...);  // 新增方法
```

#### 新架构
```cpp
// ✅ 通用数据通道，应用层决定格式
InputEvent event = CreateMouseMoveEvent(x, y);
std::vector<uint8_t> data = Serialize(event);
input_channel->Send(data.data(), data.size());

// ✅ 添加触屏支持无需修改 PeerConnection/DataChannel
TouchEvent touch = {...};
std::vector<uint8_t> data = Serialize(touch);
input_channel->Send(data.data(), data.size());  // 复用通道
```

### 动态控制

#### 旧架构
```cpp
// ❌ 禁用音频：需要在应用层不调用 SendAudioPacket
// 没有统一的控制接口
```

#### 新架构
```cpp
// ✅ 禁用音频
audio_track->SetEnabled(false);

// ✅ 动态添加音频
auto audio_track = std::make_shared<AudioTrack>();
pc.AddTrack(audio_track);

// ✅ 移除音频
pc.RemoveTrack("audio0");
```

## 五、扩展性对比

| 场景 | 旧架构 | 新架构 |
|------|--------|--------|
| **添加新媒体类型** | 修改 ConnectionManager，添加 SendXXX 方法 | 实现新的 MediaTrack 子类 |
| **添加新输入类型** | 修改 ConnectionManager，添加 SendXXX 方法 | 复用 DataChannel，应用层序列化 |
| **多路视频** | 无法实现 | 添加多个 VideoTrack |
| **自定义协议** | 修改 ConnectionManager | 实现自定义 DataChannel |
| **动态启停** | 应用层逻辑控制 | Track.SetEnabled() |

## 六、使用示例对比

### 旧架构使用

```cpp
// 初始化
ConnectionManager session;
session.Initialize(config);
session.CreateDirectConnection(remote_ip, port);

// 发送
session.SendVideoFrame(data, len, ts);
session.SendMouseMove(x, y);

// 接收
auto result = session.ReceivePacket(timeout);
if (result.IsOk()) {
  auto packet = result.Value();
  // 处理...
}
```

### 新架构使用

```cpp
// 方式 1: 使用 Session（推荐）
ControllerSession session;
session.Initialize(config);

// 自动创建 Track 和 DataChannel
session.SendVideoFrame(data, len, ts);
session.SendMouseMove(x, y);

// 方式 2: 直接使用 PeerConnection（高级）
PeerConnection pc;
pc.Initialize(config);

auto video_track = std::make_shared<VideoTrack>();
pc.AddTrack(video_track);

auto input_channel = pc.CreateDataChannel("input");

video_track->SendFrame(data, len, ts);
input_channel->Send(input_data, len);
```

## 七、总结

### 旧架构问题
1. ❌ ConnectionManager 职责过重（连接 + 媒体 + 输入）
2. ❌ 硬编码输入类型（每种输入一个方法）
3. ❌ 扩展性差（添加新功能需要修改核心类）
4. ❌ 违反单一职责原则
5. ❌ 无法动态管理媒体流

### 新架构优势
1. ✅ 职责清晰（PeerConnection 只管连接）
2. ✅ Track 抽象管理媒体流（可动态添加/移除）
3. ✅ DataChannel 通用数据传输（应用层决定格式）
4. ✅ 符合 WebRTC 标准（便于未来互操作）
5. ✅ 易扩展（添加新功能无需修改核心类）
6. ✅ 代码更简洁清晰

### 下一步
- ✅ Phase 1 完成：核心架构实现
- 🔲 Phase 2：完善 RTP 接收、多路复用
- 🔲 Phase 3：集成到主应用
- 🔲 Phase 4：TURN 支持（Phase 2 功能）
