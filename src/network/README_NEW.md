# 网络层架构说明（重构后）

参考 WebRTC 架构设计，采用 PeerConnection + MediaTrack + DataChannel 模式。

## 目录结构

```
network/
├── core/                           # 核心接口定义
│   ├── peer_connection.h/cpp      # 对等连接（核心抽象）
│   ├── media_track.h              # 媒体轨道接口
│   ├── video_track.h/cpp          # 视频轨道实现
│   ├── audio_track.h/cpp          # 音频轨道实现
│   ├── data_channel.h             # 数据通道接口
│   └── reliable_data_channel.h/cpp # 可靠数据通道实现
│
├── transport/                      # 传输层（RTP/可靠传输）
│   ├── rtp_sender.h/cpp           # RTP 发送器
│   ├── rtp_receiver.h/cpp         # RTP 接收器
│   ├── rtp_packet.h               # RTP 包定义
│   ├── pacer.h/cpp                # 发送平滑器
│   ├── jitter_buffer.h/cpp        # 抖动缓冲区
│   └── reliable_transport.h/cpp   # 可靠传输（ACK/重传）
│
├── connection/                     # 连接层（UDP 传输）
│   ├── base_connection.h          # 连接接口
│   ├── direct_connection.h/cpp    # 直连实现
│   └── turn_connection.h/cpp      # TURN 中继（Phase 2）
│
├── io/                            # 网络 I/O 层
│   └── udp_socket.h/cpp           # UDP Socket 封装
│
└── session/                       # 应用层会话（示例）
    ├── controller_session.h/cpp   # 控制端会话
    └── controlled_session.h/cpp   # 被控端会话
```

## 架构分层

```
┌─────────────────────────────────────┐
│  Session Layer (会话层)             │
│  - ControllerSession                │
│  - ControlledSession                │
└──────────────┬──────────────────────┘
               │ 使用
┌──────────────▼──────────────────────┐
│  Core Layer (核心抽象层)            │
│  - PeerConnection                   │
│  - MediaTrack (Video/Audio)         │
│  - DataChannel                      │
└──────────────┬──────────────────────┘
               │ 内部使用
┌──────────────▼──────────────────────┐
│  Transport Layer (传输层)           │
│  - RTPSender/Receiver               │
│  - ReliableTransport                │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  Connection Layer (连接层)          │
│  - DirectConnection/TurnConnection  │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  I/O Layer (网络 I/O)               │
│  - UdpSocket                        │
└─────────────────────────────────────┘
```

## 核心设计理念

1. **PeerConnection 只管连接**
   - 不知道发送的是视频还是音频
   - 不知道发送的是鼠标还是键盘
   - 只负责管理 Track 和 DataChannel

2. **MediaTrack 管理媒体流**
   - 每个 Track 独立的编码配置
   - 每个 Track 独立的 SSRC
   - 可动态添加/移除

3. **DataChannel 管理数据流**
   - 通用二进制数据传输
   - 应用层决定数据格式（JSON/Protobuf/自定义）
   - 可配置可靠性参数

## 使用示例

### 控制端发送视频

```cpp
// 1. 创建 PeerConnection
PeerConnection pc;
pc.Initialize(config);

// 2. 添加视频轨道
auto video_track = std::make_shared<VideoTrack>("video0");
pc.AddTrack(video_track);

// 3. 创建输入数据通道
auto input_channel = pc.CreateDataChannel("input", {.ordered = true});

// 4. 发送数据
video_track->SendFrame(data, size, timestamp);
input_channel->Send(input_data);
```

### 被控端接收视频

```cpp
// 1. 创建 PeerConnection
PeerConnection pc;
pc.Initialize(config);

// 2. 设置接收回调
pc.SetOnTrackCallback([](auto track) {
  if (track->GetKind() == MediaTrack::Kind::kVideo) {
    track->SetOnFrameCallback([](data, len, ts) {
      // 解码渲染
    });
  }
});

pc.SetOnDataChannelCallback([](auto channel) {
  channel->SetOnMessageCallback([](data, len) {
    // 处理输入事件
  });
});
```
