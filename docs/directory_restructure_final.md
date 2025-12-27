# ZenRemote 目录结构重构方案

## 当前问题分析

### 混乱的现状

```
src/
├── network/
│   ├── connection_manager/  # ❌ 旧代码，职责过重
│   ├── core/               # ❌ 什么是 core？不清晰
│   ├── protocol/           # ❌ RTP 等协议实现
│   ├── io/                 # ✅ Socket I/O（保留）
│   ├── transport/          # ✅ RTP/可靠传输（保留）
│   ├── session/            # ❌ 业务层不应该在 network
│   └── examples/           # ❌ 示例代码
│
├── rtc/                    # ✅ 新架构
│   ├── peer_connection/
│   ├── track/
│   └── channel/
│
├── session/                # ✅ 新架构
│   ├── controller/
│   └── controlled/
│
├── media/                  # ✅ 现有的媒体处理
│   ├── capture/
│   ├── codec/
│   ├── renderer/
│   └── sync/
│
└── 其他目录...
```

**核心问题**：
1. ❌ `network/` 目录混乱，新旧代码混杂
2. ❌ `rtc/` 目录独立存在，但只有 3 个核心类
3. ❌ `session/` 在顶层，但它依赖 `rtc/`
4. ❌ 职责不清：什么是对外接口？什么是内部实现？

---

## 核心概念梳理

### 从使用者角度看

业务代码（如 UI 层）需要什么？

```cpp
// 业务代码只需要 Session
#include "session/controller_session.h"

ControllerSession session;
session.Initialize(config);
session.SendVideoFrame(data, len, ts);  // 发送视频
```

**业务不需要直接接触**：
- ❌ PeerConnection
- ❌ VideoTrack/AudioTrack  
- ❌ DataChannel
- ❌ RTPSender
- ❌ UdpSocket

**业务只需要**：
- ✅ ControllerSession（控制端）
- ✅ ControlledSession（被控端）

### 内部依赖关系

```
Session（业务层）
    ↓ 使用
PeerConnection + Track + Channel（传输抽象层）
    ↓ 使用
RTP Sender/Receiver（协议层）
    ↓ 使用
Connection（连接层）
    ↓ 使用
UdpSocket（I/O 层）
```

---

## 推荐方案：扁平化 + 按职责分层

### 目标

1. ✅ **对外接口清晰**：业务只用 `session/`
2. ✅ **内部分层清晰**：网络、传输、连接分离
3. ✅ **避免过度嵌套**：不要太多层级
4. ✅ **复用性强**：网络层可用于其他项目

### 新目录结构

```
src/
├── session/                    # 【对外接口】业务层
│   ├── controller_session.h/cpp
│   └── controlled_session.h/cpp
│
├── transport/                  # 【核心抽象】传输抽象层
│   ├── peer_connection.h/cpp   # 对等连接
│   ├── media_track.h           # 媒体轨道接口
│   ├── video_track.h/cpp       # 视频轨道
│   ├── audio_track.h/cpp       # 音频轨道
│   ├── data_channel.h          # 数据通道接口
│   └── reliable_channel.h/cpp  # 可靠数据通道
│
├── network/                    # 【底层实现】网络传输层
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
├── media/                      # 媒体处理层（已存在）
│   ├── capture/                # 屏幕/音频采集
│   ├── codec/                  # 编解码
│   ├── renderer/               # 渲染
│   └── sync/                   # 同步
│
├── ui/                         # UI 层（已存在）
└── main.cpp
```

---

## 详细说明

### 1. session/ - 业务层（对外接口）

**职责**：提供高层 API，隐藏所有传输细节

```cpp
// 这是业务代码唯一需要的接口
#include "session/controller_session.h"

ControllerSession session;
session.Initialize(config);
session.SendVideoFrame(data, len, ts);
```

**包含**：
- `controller_session.h/cpp` - 控制端
- `controlled_session.h/cpp` - 被控端

### 2. transport/ - 传输抽象层（核心）

**职责**：提供传输抽象（PeerConnection + Track + Channel）

这是**核心架构**，Session 内部使用，但业务层不直接接触。

**包含**：
- `peer_connection.h/cpp` - 管理连接和通道
- `media_track.h` - 媒体轨道接口
- `video_track.h/cpp` - 视频轨道实现
- `audio_track.h/cpp` - 音频轨道实现
- `data_channel.h` - 数据通道接口
- `reliable_channel.h/cpp` - 可靠通道实现

**为什么叫 transport？**
- 表示"传输抽象层"
- 比 `rtc` 更通用
- 职责清晰：管理如何传输数据

### 3. network/ - 网络层（底层实现）

**职责**：纯网络传输，不涉及业务

```
network/
├── connection/      # 连接抽象（Direct/TURN）
├── rtp/            # RTP 协议实现
├── reliable/       # 可靠传输（ACK/重传）
└── io/             # Socket I/O
```

**特点**：
- ✅ 纯粹的网络代码
- ✅ 可复用到其他项目
- ✅ 不知道业务逻辑

### 4. media/ - 媒体处理层（已存在）

保持现有结构：
- `capture/` - 屏幕/音频采集
- `codec/` - H.264/Opus 编解码
- `renderer/` - 视频渲染/音频播放
- `sync/` - 音视频同步

---

## 依赖关系

```
main.cpp
    ↓
ui/ ────────→ session/        【业务只用这个】
                  ↓
              transport/       【核心抽象】
                  ↓
              network/         【底层实现】
                  
media/ ←──── session/          【媒体处理】
```

---

## 需要删除的旧代码

### ❌ 删除这些目录

```
src/network/
├── connection_manager/    # ❌ 删除（旧设计，职责过重）
├── core/                 # ❌ 删除（不清晰的命名）
├── protocol/             # ❌ 移动到 network/rtp/
├── session/              # ❌ 删除（重复，已有 src/session/）
└── examples/             # ❌ 移动到 examples/ 目录

src/rtc/                  # ❌ 删除（改名为 transport/）
```

### ✅ 保留并整理

```
src/network/io/           # ✅ 保留（UdpSocket）
src/network/transport/    # ✅ 移动到 network/rtp/ 和 network/reliable/
```

---

## 迁移步骤

### Phase 1: 重命名核心目录

1. `src/rtc/` → `src/transport/`
2. `src/network/protocol/` → `src/network/rtp/`
3. `src/network/transport/` 拆分：
   - RTP 相关 → `src/network/rtp/`
   - 可靠传输 → `src/network/reliable/`

### Phase 2: 清理旧代码

1. 删除 `src/network/connection_manager/`
2. 删除 `src/network/core/`
3. 删除 `src/network/session/`（使用 `src/session/`）
4. 移动 `src/network/examples/` → `examples/`

### Phase 3: 更新引用

更新所有 `#include` 路径：
- `#include "rtc/xxx"` → `#include "transport/xxx"`
- `#include "network/protocol/xxx"` → `#include "network/rtp/xxx"`

---

## 最终效果

### 业务代码（极简）

```cpp
#include "session/controller_session.h"

int main() {
  ControllerSession session;
  
  ControllerSession::Config config;
  config.remote_ip = "192.168.1.100";
  config.enable_video = true;
  
  session.Initialize(config);
  
  // 发送视频
  session.SendVideoFrame(data, len, ts);
  
  return 0;
}
```

### 内部实现（分层清晰）

```cpp
// session/controller_session.cpp
#include "transport/peer_connection.h"
#include "transport/video_track.h"
#include "transport/data_channel.h"

void ControllerSession::Initialize() {
  pc_ = std::make_unique<PeerConnection>();
  video_track_ = std::make_shared<VideoTrack>();
  pc_->AddTrack(video_track_);
}
```

```cpp
// transport/video_track.cpp
#include "network/rtp/rtp_sender.h"

void VideoTrack::SendFrame() {
  rtp_sender_->SendVideoFrame(...);
}
```

```cpp
// network/rtp/rtp_sender.cpp
#include "network/connection/base_connection.h"

void RTPSender::SendVideoFrame() {
  connection_->Send(...);
}
```

---

## 总结

### 核心思想

1. **对外接口**：`session/`（业务只用这个）
2. **传输抽象**：`transport/`（PeerConnection + Track + Channel）
3. **网络实现**：`network/`（RTP, Connection, Socket）
4. **媒体处理**：`media/`（采集、编解码、渲染）

### 关键改进

| 改进 | 说明 |
|------|------|
| ✅ 扁平化 | 只有 4 个顶层目录 |
| ✅ 职责清晰 | session 业务，transport 抽象，network 实现 |
| ✅ 对外简单 | 业务只用 session |
| ✅ 内部分层 | transport → network 依赖清晰 |
| ✅ 命名直观 | transport 比 rtc 更清楚 |

### 为什么叫 transport？

- `transport` = 传输抽象层
- 包含 PeerConnection, Track, Channel
- 比 `rtc` 更通用、更清晰
- 职责明确：管理"如何传输数据"

**这才是清晰、实用、易维护的架构！** 🎯
