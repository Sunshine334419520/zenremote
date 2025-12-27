# ZenRemote 目录结构说明

## 整体架构分层

参考 WebRTC 架构，采用清晰的分层设计：

```
src/
├── network/              # 网络传输层（纯传输，不涉及业务）
│   ├── io/              # Socket I/O
│   ├── transport/       # RTP/可靠传输协议
│   └── connection/      # 连接抽象
│
├── rtc/                 # RTC 抽象层（WebRTC 风格）
│   ├── peer_connection/ # 对等连接
│   ├── track/          # 媒体轨道（Video/Audio）
│   └── channel/        # 数据通道（DataChannel）
│
├── session/            # 应用会话层
│   ├── controller/     # 控制端会话
│   └── controlled/     # 被控端会话
│
├── media/              # 媒体处理（编解码、采集、渲染）
├── ui/                 # 用户界面
└── main.cpp
```

## 详细说明

### 1. network/ - 网络传输层

**职责**：纯网络传输，不涉及业务逻辑

```
network/
├── io/
│   └── udp_socket.h/cpp           # UDP Socket 封装
│
├── transport/
│   ├── rtp_sender.h/cpp           # RTP 发送器
│   ├── rtp_receiver.h/cpp         # RTP 接收器
│   ├── rtp_packet.h               # RTP 包定义
│   └── reliable_transport.h/cpp   # 可靠传输（ACK/重传）
│
└── connection/
    ├── base_connection.h          # 连接接口
    ├── direct_connection.h/cpp    # 直连实现
    └── turn_connection.h/cpp      # TURN 中继
```

**特点**：
- ✅ 只处理字节流传输
- ✅ 不知道传输的是视频、音频还是数据
- ✅ 可复用于其他项目

### 2. rtc/ - RTC 抽象层

**职责**：提供 WebRTC 风格的抽象，管理连接和传输通道

```
rtc/
├── peer_connection/
│   └── peer_connection.h/cpp      # 对等连接（核心抽象）
│
├── track/
│   ├── media_track.h              # 媒体轨道接口
│   ├── video_track.h/cpp          # 视频轨道
│   └── audio_track.h/cpp          # 音频轨道
│
└── channel/
    ├── data_channel.h             # 数据通道接口
    └── reliable_data_channel.h/cpp # 可靠数据通道
```

**特点**：
- ✅ PeerConnection 只管连接，不管内容
- ✅ Track 抽象管理媒体流
- ✅ DataChannel 抽象管理数据流
- ✅ 符合 WebRTC 标准

### 3. session/ - 应用会话层

**职责**：实现具体业务逻辑

```
session/
├── controller/
│   └── controller_session.h/cpp   # 控制端会话
│
└── controlled/
    └── controlled_session.h/cpp   # 被控端会话
```

**特点**：
- ✅ 使用 RTC 抽象层
- ✅ 实现远程桌面业务逻辑
- ✅ 管理 Track 和 DataChannel

## 使用示例

### 控制端（发送屏幕）

```cpp
#include "session/controller/controller_session.h"

using namespace session;

ControllerSession session;

// 配置
ControllerSession::Config config;
config.remote_ip = "192.168.1.100";
config.remote_port = 50000;
config.enable_video = true;

// 初始化
session.Initialize(config);

// 发送视频帧
session.SendVideoFrame(frame_data, frame_size, timestamp);
```

### 被控端（接收屏幕，发送输入）

```cpp
#include "session/controlled/controlled_session.h"

using namespace session;

ControlledSession session;

// 设置接收回调
session.SetOnVideoFrameCallback([](const uint8_t* data, size_t len, uint32_t ts) {
  // 解码渲染
});

// 初始化
ControlledSession::Config config;
config.local_port = 50000;
session.Initialize(config);

// 发送输入事件
session.SendMouseMove(x, y);
session.SendKeyEvent(key_code, is_down, modifiers);
```

## 分层职责对比

| 层次 | 职责 | 不负责 |
|------|------|--------|
| **network** | Socket I/O, RTP 打包, 可靠传输 | 不知道传输的是什么内容 |
| **rtc** | 连接管理, Track/Channel 抽象 | 不知道具体业务逻辑 |
| **session** | 远程桌面业务逻辑 | 不处理网络细节 |

## 为什么这样分层？

### ❌ 错误：全部放在 network/

```
network/
├── io/
├── transport/
├── connection/
├── core/              # ❌ 这是 RTC 抽象，不是网络
│   ├── peer_connection/
│   ├── track/
│   └── channel/
└── session/           # ❌ 这是业务逻辑，不是网络
    ├── controller/
    └── controlled/
```

**问题**：
- 网络层混入了业务逻辑
- 无法复用网络层到其他项目
- 职责不清晰

### ✅ 正确：按职责分层

```
network/     # 纯传输，可复用
rtc/         # RTC 抽象，参考 WebRTC
session/     # 业务逻辑，特定于远程桌面
```

**优势**：
- 职责清晰
- network 层可复用
- rtc 层符合标准
- session 层实现业务

## 总结

- **network/** = 传输字节流
- **rtc/** = WebRTC 抽象（PeerConnection + Track + DataChannel）
- **session/** = 远程桌面业务逻辑

这才是正确的目录结构！🎯
