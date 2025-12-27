# 架构对比：错误 vs 正确

## ❌ 错误的设计（之前）

### 依赖关系混乱

```
应用层
  ├─ ConnectionManager  ← 用来管理连接
  └─ RTPSender ???      ← 怎么用？需要 BaseConnection

问题：
1. RTPSender 需要 BaseConnection
2. BaseConnection 在 ConnectionManager 内部
3. 应用层拿不到 BaseConnection
4. 无法使用 RTPSender ❌
```

### 应用层代码混乱

```cpp
// ❌ 错误用法
class ControllerSession {
  ConnectionManager conn_mgr_;
  RTPSender* rtp_sender_;  // 怎么初始化？

  void SendFrame(const VideoFrame& frame) {
    // 方案 1: 直接用 ConnectionManager 发送？
    conn_mgr_.Send(frame.data, frame.size);  // 但没有 RTP 打包！

    // 方案 2: 用 RTPSender？
    rtp_sender_->SendVideoFrame(...);  // 但 RTPSender 需要 BaseConnection
                                       // BaseConnection 在 conn_mgr_ 内部
                                       // 拿不到！❌
  }
};
```

### 职责不清

```
ConnectionManager:
  - 只管理连接？
  - 还是也负责发送？
  - 谁负责 RTP 打包？
  
RTPSender:
  - 需要 BaseConnection
  - 但应用层拿不到
  - 到底怎么用？
```

---

## ✅ 正确的设计（现在）

### 清晰的分层

```
┌────────────────────────────────────────┐
│  应用层                                │
│  - ControllerSession                   │
│  - ControlledSession                   │
│  ↓ 只依赖 ConnectionManager            │
└────────────────────────────────────────┘
                 ↓
┌────────────────────────────────────────┐
│  ConnectionManager (会话层)            │
│  ┌──────────────────────────────────┐  │
│  │ 提供高层接口：                   │  │
│  │ - SendVideoFrame()               │  │
│  │ - SendMouseMove()                │  │
│  │ - ReceivePacket()                │  │
│  └──────────────────────────────────┘  │
│                                        │
│  ┌──────────────────────────────────┐  │
│  │ 内部管理（应用层不可见）：       │  │
│  │ - RTPSender                      │  │
│  │ - RTPReceiver                    │  │
│  │ - ReliableInputSender            │  │
│  │ - BaseConnection (Direct/TURN)   │  │
│  └──────────────────────────────────┘  │
└────────────────────────────────────────┘
                 ↓
┌────────────────────────────────────────┐
│  传输层 (DirectConnection/TURN)        │
└────────────────────────────────────────┘
                 ↓
┌────────────────────────────────────────┐
│  I/O 层 (UdpSocket)                    │
└────────────────────────────────────────┘
```

### 应用层代码清晰

```cpp
// ✅ 正确用法
class ControllerSession {
  ConnectionManager conn_mgr_;  // 唯一依赖！

  void SendFrame(const VideoFrame& frame) {
    // 一行搞定 - 内部自动 RTP 打包
    conn_mgr_.SendVideoFrame(
      frame.data, frame.size, 
      frame.timestamp, frame.is_last
    );
  }

  void OnMouseMove(int x, int y) {
    // 一行搞定 - 内部自动可靠传输
    conn_mgr_.SendMouseMove(x, y);
  }

  void ReceiveAndProcess() {
    // 一行搞定 - 自动解析 RTP
    auto result = conn_mgr_.ReceivePacket(100);
    if (result.IsOk()) {
      auto data = result.Value();
      if (data.payload_type == 96) {  // 视频
        DecodeFrame(data.payload);
      }
    }
  }
};
```

### 职责清晰

| 层次 | 职责 | 应用层可见 |
|------|------|-----------|
| **ConnectionManager** | 会话管理 | ✅ 可见 |
| - 提供高层接口 | SendVideoFrame, SendMouseMove | ✅ |
| - 管理协议层 | RTPSender, ReliableInput | ❌ 内部 |
| - 管理连接 | Direct/TURN 切换 | ❌ 内部 |
| **RTPSender** | RTP 打包 | ❌ 内部 |
| **BaseConnection** | 连接抽象 | ❌ 内部 |
| **UdpSocket** | Socket I/O | ❌ 内部 |

---

## 对比总结

| 维度 | 错误设计 ❌ | 正确设计 ✅ |
|------|-----------|-----------|
| **应用层依赖** | ConnectionManager + RTPSender | 只依赖 ConnectionManager |
| **使用复杂度** | 需要手动 RTP 打包 | 一行代码搞定 |
| **职责划分** | 混乱，不清楚谁负责什么 | 清晰，每层职责明确 |
| **代码行数** | 10+ 行发送一个帧 | 1 行发送一个帧 |
| **易用性** | ⭐⭐ | ⭐⭐⭐⭐⭐ |
| **扩展性** | Phase 2 需要改应用层 | Phase 2 应用层零改动 |
| **符合直觉** | 不符合 | 完全符合 |

---

## 正确设计的关键点

### 1. ConnectionManager = Session Manager

```cpp
// 不只是管理连接，而是管理整个会话
class ConnectionManager {
  // 高层接口（应用层可见）
  SendVideoFrame(...);
  SendMouseMove(...);
  ReceivePacket(...);

 private:
  // 内部实现（应用层不可见）
  RTPSender rtp_sender_;
  RTPReceiver rtp_receiver_;
  ReliableInputSender input_sender_;
  BaseConnection* active_connection_;
};
```

### 2. 应用层极简

```cpp
// 3 行代码完成初始化和发送
ConnectionManager mgr;
mgr.Initialize({remote_ip: "192.168.1.100"});
mgr.SendVideoFrame(frame.data, frame.size, timestamp);
```

### 3. 内部封装

```cpp
// 应用层调用
mgr.SendVideoFrame(data, size, timestamp);

// 内部实现
SendVideoFrame(...) {
  // 1. RTP 打包
  rtp_sender_->SendVideoFrame(...);
  
  // 2. 流量控制
  pacer_->CanSend();
  
  // 3. 发送
  connection_->Send(...);
}
```

---

## 实际使用示例

### 控制端（发送视频）

```cpp
// 初始化
ConnectionManager session;
session.Initialize({
  mode: Mode::kDirect,
  remote_ip: "192.168.1.100",
  remote_port: 50000
});
session.Connect();

// 发送循环
while (running) {
  VideoFrame frame = camera.CaptureFrame();
  
  // ✅ 一行代码搞定！
  session.SendVideoFrame(
    frame.data, frame.size,
    GetTimestamp90kHz(),
    true  // is_last_packet
  );
}
```

### 被控端（接收视频，发送输入）

```cpp
// 初始化
ConnectionManager session;
session.Initialize({local_port: 50000});
session.Connect();

// 接收循环
while (running) {
  // ✅ 一行代码接收并解析
  auto result = session.ReceivePacket(100);
  if (result.IsOk()) {
    auto data = result.Value();
    
    if (data.payload_type == 96) {  // 视频
      decoder.Decode(data.payload);
      renderer.Render(decoded_frame);
    }
  }
}

// 输入事件处理
void OnMouseMove(int x, int y) {
  // ✅ 一行代码发送，自动可靠传输
  session.SendMouseMove(x, y);
}
```

---

## 结论

**正确的设计应该是**：

1. ✅ ConnectionManager = Session Manager（会话层）
2. ✅ 提供高层接口，应用层无需知道内部实现
3. ✅ 内部管理协议层（RTP + ReliableInput）
4. ✅ 应用层一行代码搞定发送/接收
5. ✅ Phase 2 扩展对应用透明

**这才是合理的、易用的、可扩展的架构！** 🎯
