# 网络层架构重新设计 - 正确版本

## 问题分析

### 原设计的问题

```cpp
// ❌ 错误的依赖关系
RTPSender → BaseConnection (直接依赖传输层)
应用层 → ConnectionManager → BaseConnection

// 问题：
// 1. RTPSender 和 ConnectionManager 都依赖 BaseConnection
// 2. 应用层无法同时使用 RTPSender 和 ConnectionManager
// 3. 职责混乱：到底谁负责发送？
```

## 正确的架构

### 方案 1: ConnectionManager 提供 RTP 接口（推荐）

```
┌────────────────────────────────────────────┐
│  应用层 (Application)                      │
│  只依赖 ConnectionManager                  │
│  controller.SendVideoFrame(frame)          │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────────────────────────────────┐
│  ConnectionManager (Session Layer)         │
│  - SendVideoFrame()  ← 提供 RTP 接口      │
│  - SendAudioPacket()                       │
│  - SendInputEvent()                        │
│  ↓ 内部使用                                │
│  - RTPSender                               │
│  - RTPReceiver                             │
│  - ReliableInputSender                     │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────────────────────────────────┐
│  Transport Layer                           │
│  - BaseConnection                          │
│  - DirectConnection / TurnConnection       │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────────────────────────────────┐
│  I/O Layer                                 │
│  - UdpSocket                               │
└────────────────────────────────────────────┘
```

**关键改进**：
- ✅ 应用层只依赖 `ConnectionManager`
- ✅ `ConnectionManager` 内部管理 `RTPSender/Receiver`
- ✅ 应用层不需要知道 RTP 细节
- ✅ 职责清晰：ConnectionManager = Session Manager

### 方案 2: 分离 Session 和 Connection（备选）

```
┌────────────────────────────────────────────┐
│  应用层                                    │
│  - ControllerSession                       │
│  - ControlledSession                       │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────────────────────────────────┐
│  Session Layer                             │
│  - ControllerSession 内部使用：            │
│    * ConnectionManager (管理连接)         │
│    * RTPSender (打包发送)                 │
│    * RTPReceiver (接收解析)               │
└────────────────┬──────────────────────────┘
                 ↓
        ConnectionManager
```

**对比**：
| 维度 | 方案 1 | 方案 2 |
|------|--------|--------|
| 复杂度 | 简单 | 复杂 |
| 灵活性 | 中等 | 高 |
| 易用性 | 高 | 中等 |
| 推荐度 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |

## 推荐方案：ConnectionManager = Session Manager

### 重命名建议

```cpp
ConnectionManager → SessionManager

// 更准确地反映职责：
// - 管理会话生命周期
// - 管理连接（Direct/TURN）
// - 管理协议（RTP/Input）
```

### 新的接口设计

```cpp
class SessionManager {
 public:
  // 会话管理
  Result<void> Initialize(const Config& config);
  Result<void> Connect();
  void Disconnect();

  // 媒体发送（应用层接口）
  Result<void> SendVideoFrame(const uint8_t* data, size_t length, 
                              uint32_t timestamp_90khz, bool marker);
  Result<void> SendAudioPacket(const uint8_t* data, size_t length,
                               uint32_t timestamp_48khz);

  // 输入发送（应用层接口）
  Result<void> SendMouseMove(uint16_t x, uint16_t y);
  Result<void> SendKeyEvent(uint32_t key_code, bool is_down);

  // 接收接口
  Result<ReceivedPacket> ReceivePacket(int timeout_ms);

 private:
  // 内部组件（应用层不可见）
  std::unique_ptr<BaseConnection> connection_;  // Direct 或 TURN
  std::unique_ptr<RTPSender> rtp_sender_;
  std::unique_ptr<RTPReceiver> rtp_receiver_;
  std::unique_ptr<ReliableInputSender> input_sender_;
};
```

### 使用示例

```cpp
// 应用层代码 - 非常简洁
class ControllerSession {
 public:
  Result<void> Initialize(const std::string& remote_ip) {
    SessionManager::Config config;
    config.remote_ip = remote_ip;
    config.remote_port = 50000;
    
    auto result = session_mgr_.Initialize(config);
    if (result.IsErr()) return result;
    
    return session_mgr_.Connect();
  }

  void SendVideoFrame(const VideoFrame& frame) {
    // 直接发送，无需关心 RTP 打包
    session_mgr_.SendVideoFrame(
      frame.data, frame.size, 
      frame.timestamp, frame.is_last
    );
  }

  void OnMouseMove(int x, int y) {
    // 直接发送，无需关心可靠性机制
    session_mgr_.SendMouseMove(x, y);
  }

 private:
  SessionManager session_mgr_;  // 唯一依赖
};
```

## 对比：修改前 vs 修改后

### 修改前（混乱）❌

```cpp
// 应用层代码复杂
class ControllerSession {
  ConnectionManager conn_mgr_;
  RTPSender* rtp_sender_;  // 怎么初始化？
  
  void SendFrame() {
    // 需要自己打包 RTP
    RTPPacket packet = ...;
    rtp_sender_->Send(...);  // 但 RTPSender 需要 BaseConnection
                             // ConnectionManager 封装了 BaseConnection
                             // 怎么传递？❌
  }
};
```

### 修改后（清晰）✅

```cpp
// 应用层代码简洁
class ControllerSession {
  SessionManager session_mgr_;  // 一个就够了
  
  void SendFrame(const VideoFrame& frame) {
    // 一行搞定，内部自动 RTP 打包 + 发送
    session_mgr_.SendVideoFrame(frame.data, frame.size, frame.timestamp);
  }
};
```

## 总结

**正确的架构应该是**：

1. **SessionManager** (原 ConnectionManager)
   - 应用层唯一依赖
   - 提供高层接口：SendVideoFrame, SendMouseMove 等
   - 内部管理：Connection + RTP + ReliableInput

2. **应用层代码**
   - 只依赖 SessionManager
   - 调用高层接口，无需关心：
     * RTP 打包细节
     * 连接类型（Direct/TURN）
     * 可靠性机制

3. **优势**
   - ✅ 职责清晰：SessionManager = 完整的会话管理
   - ✅ 易用性高：应用层一行代码搞定发送
   - ✅ 扩展性好：Phase 2 添加 TURN 对应用透明
   - ✅ 符合直觉：发送视频就调 SendVideoFrame

这才是合理的设计！
