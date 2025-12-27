# ZenRemote 最终目录结构

## 目录组织

```
src/
├── session/                    # 应用会话层（对外接口）
│   ├── controller_session.h/cpp
│   └── controlled_session.h/cpp
│
├── transport/                  # 传输抽象层
│   ├── peer_connection.h/cpp   # 对等连接
│   ├── media_track.h           # 媒体轨道接口
│   ├── video_track.h/cpp       # 视频轨道
│   ├── audio_track.h/cpp       # 音频轨道
│   ├── data_channel.h          # 数据通道接口
│   └── reliable_channel.h/cpp  # 可靠数据通道
│
├── network/                    # 网络实现层
│   ├── connection/             # 连接层
│   │   ├── base_connection.h
│   │   ├── direct_connection.h/cpp
│   │   └── turn_connection.h/cpp
│   │
│   ├── rtp/                    # RTP 协议
│   │   ├── rtp_sender.h/cpp
│   │   ├── rtp_receiver.h/cpp
│   │   └── rtp_packet.h
│   │
│   ├── reliable/               # 可靠传输
│   │   └── reliable_transport.h/cpp
│   │
│   └── io/                     # Socket I/O
│       └── udp_socket.h/cpp
│
├── media/                      # 媒体处理层
│   ├── capture/                # 屏幕/音频采集
│   ├── codec/                  # 编解码
│   ├── renderer/               # 渲染
│   └── sync/                   # 同步
│
├── ui/                         # UI 层
└── main.cpp
```

## 分层说明

### 1. session/ - 应用会话层

**对外接口，业务层直接使用**

```cpp
#include "session/controller_session.h"

session::ControllerSession session;
session.Initialize(config);
session.SendVideoFrame(data, len, ts);
```

**包含**：
- `controller_session` - 控制端会话
- `controlled_session` - 被控端会话

**职责**：
- ✅ 提供高层 API
- ✅ 隐藏传输细节
- ✅ 实现业务逻辑

### 2. transport/ - 传输抽象层

**核心抽象，session 内部使用**

```cpp
transport::PeerConnection pc;
auto video_track = std::make_shared<transport::VideoTrack>();
pc.AddTrack(video_track);
```

**包含**：
- `peer_connection` - 管理连接和通道
- `media_track/video_track/audio_track` - 媒体轨道
- `data_channel/reliable_channel` - 数据通道

**职责**：
- ✅ 抽象传输通道（Track + Channel）
- ✅ 管理连接生命周期
- ✅ 不关心具体业务

### 3. network/ - 网络实现层

**底层实现，transport 内部使用**

```
network/
├── connection/   # 连接抽象（Direct/TURN）
├── rtp/         # RTP 协议实现
├── reliable/    # 可靠传输（ACK/重传）
└── io/          # Socket I/O
```

**职责**：
- ✅ 纯网络传输
- ✅ 协议实现
- ✅ 可复用到其他项目

### 4. media/ - 媒体处理层

**已存在，保持现有结构**

- `capture/` - 屏幕/音频采集
- `codec/` - H.264/Opus 编解码
- `renderer/` - 视频渲染/音频播放
- `sync/` - 音视频同步

## 依赖关系

```
main.cpp
    ↓
ui/ ──────→ session/        【业务只用这个】
                ↓
            transport/       【传输抽象】
                ↓
            network/         【底层实现】
                
media/ ←──── session/        【媒体处理】
```

## 使用示例

### 控制端

```cpp
#include "session/controller_session.h"

int main() {
  session::ControllerSession session;
  
  session::ControllerSession::Config config;
  config.remote_ip = "192.168.1.100";
  config.enable_video = true;
  
  session.Initialize(config);
  session.SendVideoFrame(data, len, ts);
  
  return 0;
}
```

### 被控端

```cpp
#include "session/controlled_session.h"

int main() {
  session::ControlledSession session;
  
  session.SetOnVideoFrameCallback([](const uint8_t* data, size_t len, uint32_t ts) {
    // 解码渲染
  });
  
  session::ControlledSession::Config config;
  config.local_port = 50000;
  session.Initialize(config);
  
  session.SendMouseMove(x, y);
  
  return 0;
}
```

## 核心特点

1. ✅ **分层清晰**
   - session（业务）→ transport（抽象）→ network（实现）

2. ✅ **职责明确**
   - session: 应用逻辑
   - transport: 传输抽象
   - network: 网络实现

3. ✅ **易于使用**
   - 业务只用 `session/controller_session.h`
   - 一行代码发送视频

4. ✅ **易于扩展**
   - network 层可复用
   - transport 层可扩展协议
   - session 层可添加新业务

5. ✅ **符合标准**
   - 参考 WebRTC 架构
   - PeerConnection + Track + Channel 模式

## 已删除的旧代码

- ❌ `network/connection_manager/` - 旧设计，职责过重
- ❌ `network/core/` - 命名不清晰
- ❌ `network/protocol/` - 已移到 `network/rtp/`
- ❌ `network/transport/` - 已拆分到 `network/rtp/` 和 `network/reliable/`
- ❌ `network/session/` - 重复，已有 `src/session/`
- ❌ `network/examples/` - 移到顶层 `examples/`
- ❌ `src/rtc/` - 已重命名为 `src/transport/`

## 总结

**这是最终的、清晰的、实用的架构！**

- **session/** = 业务接口
- **transport/** = 传输抽象
- **network/** = 网络实现
- **media/** = 媒体处理

分层清晰，职责明确，易于使用和扩展！🎯
