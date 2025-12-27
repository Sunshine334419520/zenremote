# 远程桌面项目技术架构设计文档

## 项目概述

**项目名称**: ZenRemote (建议命名)  
**目标**: 参考 WebRTC 技术栈,自主实现跨平台(Windows/macOS)远程桌面控制系统  
**核心能力**: 双端对等(任一端可发起控制),实现屏幕共享、鼠标键盘控制、音频传输

**角色模型**:
- 架构层面不区分“控制端/被控端”的代码结构;两端均具备采集、编解码、传输、渲染与输入能力。
- 仅在业务层以“会话角色”区分当前端处于 `Controller`(发起控制) 或 `Controlled`(被控制) 状态,并可在会话内切换角色(类似 TeamViewer)。

**开发策略**: 
- **Phase 1 (2-3个月)**: 局域网直连版本,调通所有核心功能(采集、编解码、传输、控制、渲染)
- **Phase 2 (1-2个月)**: 添加公网支持,部署 TURN 服务器实现跨网段远程控制

---

## 一、技术架构设计

### 1.1 整体架构

#### **Phase 1: 局域网直连架构 (当前目标)**

```
┌─────────────────────────────────────────────────────────────┐
│                      端点 A (Endpoint A)                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ 输入捕获  │  │ 视频解码  │  │ 音频播放  │  │ UI 渲染   │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│         │              ▲              ▲              │       │
│         │              │              │              │       │
│         ▼              │              │              ▼       │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         网络传输层 (UDP 直连)                         │  │
│  │         IP: 192.168.x.x (局域网地址)                 │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ RTP/自定义协议
                              │ (局域网内直接通信)
                              │
┌─────────────────────────────────────────────────────────────┐
│                      端点 B (Endpoint B)                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ 屏幕采集  │  │ 视频编码  │  │ 音频采集  │  │ 输入注入  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│         │              │              │              ▲       │
│         │              │              │              │       │
│         ▼              ▼              ▼              │       │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         网络传输层 (UDP 直连)                         │  │
│  │         监听端口: 50000 (可配置)                     │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

**Phase 1 特点**:
- ✅ 无需信令服务器(手动输入对方 IP)
- ✅ 无需 STUN/TURN/ICE (局域网直连)
- ✅ 简单高效,延迟最低(< 50ms)
- ✅ 专注核心功能开发

---

#### **Phase 2: 公网支持架构 (后续扩展,无 STUN/ICE)**

```
控制端 (公网)  ←→  TURN 服务器  ←→  被控端 (公网)
     │                  │                  │
     └──────────── 信令服务器 ────────────┘
            (交换连接信息,协商中继)
```

**Phase 2 新增**:
- 信令服务器(WebSocket,交换连接信息与角色)
- TURN 服务器(仅中继模式,不进行 STUN/ICE)
- 直连优先策略(同网段直连;跨网段直接使用 TURN)

### 1.2 核心模块划分

#### **模块 1: 连接管理 (Connection Manager)**

**Phase 1 实现 (局域网直连)**:
- **职责**: 管理 UDP Socket,建立点对点连接
- **技术栈**: 
  - 标准 Socket API (Windows: Winsock2, macOS: BSD Sockets)
  - 配置文件指定对方 IP 地址和端口
  - 简单的握手协议(确认连接建立)
- **实现要点**:
  ```cpp
  class DirectConnection {
      bool Connect(const std::string& remote_ip, uint16_t port);
      bool Send(const uint8_t* data, size_t len);
      bool Receive(uint8_t* buffer, size_t* len);
  };
  ```

**Phase 2 扩展 (公网支持)**:
- 添加信令客户端(连接信令服务器)
- 添加 TURN 客户端(中继模式)
- 保留直连作为优先选项

#### **模块 2: 媒体采集 (Media Capture)**
- **屏幕采集**:
  - Windows: Desktop Duplication API (DXGI)
  - macOS: SCStreamCaptureKit (macOS 12.3+) 或 CGDisplayStream
- **音频采集**:
  - Windows: WASAPI
  - macOS: Core Audio
- **帧率**: 30-60 FPS (可配置)

#### **模块 3: 视频编解码 (Video Codec)**
- **编码器**: 
  - 硬件加速: H.264 (VideoToolbox/NVENC/QSV)
  - 软件后备: libx264 (FFmpeg)
- **码率控制**: VBR, 1-5 Mbps 可调
- **关键帧**: 每 2-3 秒插入

#### **模块 4: 音频编解码 (Audio Codec)**
- **编码器**: Opus (低延迟,高质量)
- **采样率**: 48kHz
- **码率**: 32-64 kbps

#### **模块 5: 网络传输 (Network Transport)**

**Phase 1 实现 (局域网)**:
- **协议**: UDP (原始 Socket)
- **可靠性**: 简单的序列号 + ACK 机制(仅用于控制消息)
- **媒体流**: 实时传输,允许丢包(视频帧)
- **特点**: 局域网环境稳定,无需复杂拥塞控制

#### **模块 6: Jitter Buffer (抖动缓冲区)**

参考 WebRTC 的 `FrameBuffer` 和 `NetEq` 实现:

**核心职责**:
- **接收端重排序**: 处理乱序到达的 RTP 包
- **延迟估算**: 动态计算网络抖动(Jitter)
- **自适应缓冲**: 根据网络状况调整缓冲深度
- **丢包处理**: 检测丢包,触发 NACK 或跳过帧
- **音视频对齐**: 基于 RTP 时间戳同步音视频

**WebRTC 参考实现** (`modules/video_coding/frame_buffer2.cc`):
```cpp
class JitterBuffer {
    // 核心参数
    const int kMaxFrameDelayMs = 100;      // 最大延迟阈值
    const int kMinBufferLevelMs = 20;      // 最小缓冲深度
    const int kMaxBufferLevelMs = 150;     // 最大缓冲深度
    
    // 核心方法
    bool InsertPacket(const RtpPacket& packet);  // 插入包
    Frame* GetNextFrame();                       // 获取下一帧
    void UpdateJitter(uint32_t arrival_time);    // 更新抖动估计
    void AdjustBufferLevel();                    // 动态调整缓冲
};
```

**关键算法**:
1. **延迟计算** (RFC 3550):
   ```
   D(i) = (Arrival_time(i) - RTP_timestamp(i)) - (Arrival_time(i-1) - RTP_timestamp(i-1))
   Jitter = Jitter + (|D(i)| - Jitter) / 16  // 平滑滤波
   ```

2. **自适应缓冲**:
   - 网络稳定: 减少缓冲深度(降低延迟)
   - 网络抖动大: 增加缓冲深度(避免卡顿)
   - 目标: `BufferLevel = BaseDelay + 4 * Jitter`

3. **丢包检测**:
   - 序列号不连续 → 触发 NACK(Phase 2)
   - 超时未到达 → 标记丢失,跳过该帧

**Phase 1 简化实现**:
```cpp
class SimpleJitterBuffer {
private:
    std::map<uint32_t, RtpPacket> buffer_;  // 按序列号存储
    uint32_t next_seq_ = 0;                 // 期望的下一个序列号
    int64_t base_delay_ms_ = 50;            // 基础延迟
    
public:
    void Insert(const RtpPacket& packet) {
        buffer_[packet.seq] = packet;
        // 局域网版本: 固定 50ms 延迟即可
    }
    
    std::optional<RtpPacket> Pop() {
        auto now = GetCurrentTimeMs();
        if (buffer_.empty()) return std::nullopt;
        
        auto& [seq, packet] = *buffer_.begin();
        // 检查是否到达播放时间
        if (now >= packet.arrival_time + base_delay_ms_) {
            auto result = packet;
            buffer_.erase(buffer_.begin());
            return result;
        }
        return std::nullopt;
    }
};
```

#### **模块 7: 发送端平滑发送 (Pacer)**

参考 WebRTC 的 `PacedSender` 实现:

**核心职责**:
- **码率控制**: 避免突发流量造成网络拥塞
- **帧间平滑**: 将一帧数据分散到多个时间片发送
- **优先级调度**: 关键帧优先,音频优先于视频
- **拥塞避免**: 配合 GCC 算法动态调整发送速率

**WebRTC 参考实现** (`modules/pacing/paced_sender.cc`):
```cpp
class PacedSender {
private:
    const int64_t kMinPacketIntervalUs = 5000;  // 最小包间隔 5ms
    const double kPacingFactor = 2.5;           // Pacing 倍数
    
    struct Packet {
        Priority priority;  // HIGH(音频/关键帧) / NORMAL(P帧)
        uint8_t* data;
        size_t size;
        int64_t enqueue_time_us;
    };
    
    std::priority_queue<Packet> queue_;  // 按优先级排序
    int64_t target_bitrate_bps_ = 2000000;  // 目标码率
    
public:
    void EnqueuePacket(Packet packet) {
        queue_.push(packet);
    }
    
    void ProcessQueue() {
        while (!queue_.empty()) {
            auto packet = queue_.top();
            
            // 计算发送间隔: packet_size * 8 / (bitrate * pacing_factor)
            int64_t interval_us = packet.size * 8 * 1000000 / 
                                  (target_bitrate_bps_ * kPacingFactor);
            interval_us = std::max(interval_us, kMinPacketIntervalUs);
            
            // 等待间隔后发送
            std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
            SendPacket(packet);
            queue_.pop();
        }
    }
};
```

**关键算法**:
1. **Pacing Rate 计算**:
   ```
   Pacing_Rate = Target_Bitrate * Pacing_Factor
   Packet_Interval = Packet_Size / Pacing_Rate
   ```
   - `Pacing_Factor` 通常为 2.5 (允许 2.5 倍突发)
   - 目标码率 2Mbps → Pacing Rate 5Mbps

2. **突发控制**:
   - I 帧(300KB) 不能一次性发送,分散到 50-100ms
   - 每 5ms 发送一个 RTP 包(~15KB)

3. **优先级队列**:
   ```
   Priority:  音频 > 视频 I 帧 > 视频 P 帧
   ```

**Phase 1 简化实现**:
```cpp
class SimplePacer {
private:
    std::queue<RtpPacket> queue_;
    int64_t last_send_time_us_ = 0;
    const int64_t kPacketIntervalUs = 5000;  // 固定 5ms 间隔
    
public:
    void Enqueue(RtpPacket packet) {
        queue_.push(packet);
    }
    
    void Process() {
        while (!queue_.empty()) {
            auto now_us = GetCurrentTimeMicros();
            auto elapsed = now_us - last_send_time_us_;
            
            if (elapsed >= kPacketIntervalUs) {
                SendPacket(queue_.front());
                queue_.pop();
                last_send_time_us_ = now_us;
            } else {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(kPacketIntervalUs - elapsed));
            }
        }
    }
};
```

**Phase 2 增强**:
- 集成 GCC 拥塞控制,动态调整 `target_bitrate_bps_`
- 添加丢包重传队列(NACK 包优先发送)
- 实现完整的优先级调度

**Phase 2 扩展 (公网)**:
- **拥塞控制**: GCC (Google Congestion Control) 算法
  - 基于延迟的码率估计 (Delay-based)
  - 基于丢包的码率估计 (Loss-based)
  - 两者结合,选择较小值作为目标码率
  - 参考: `modules/congestion_controller/goog_cc/`
- **FEC**: 前向纠错 (降低丢包影响)
  - 使用 Reed-Solomon 或 XOR 编码
  - 冗余度 10-30% 可配置
- **NACK**: 选择性重传 (针对关键帧)
  - 检测到丢包后发送 RTCP NACK
  - 重传队列与 Pacer 集成
- **TURN 中继**: 通过服务器转发流量

#### **模块 8: 输入控制 (Input Control) - 可靠传输**

**关键设计决策**: 使用 UDP + 应用层可靠性 (序列号 + ACK + 重传)

**为什么选择 UDP?**
| 对比项 | UDP | TCP | DataChannel |
|-------|-----|-----|-----------|
| **延迟** | 极低(实时) | 较高(慢启动) | 高(WebRTC 库开销) |
| **建立成本** | 无连接开销 | 3-way handshake | 需要完整的 WebRTC 栈 |
| **局域网可靠性** | 良好(误包率<0.1%) | 完美 | 过度设计 |
| **复杂度** | 低 | 中 | 高 |
| **与媒体流集成** | 原生支持(同 Socket) | 需要独立连接 | 需要独立通道 |
| **阻塞行为** | 非阻塞,丢包不卡 | 阻塞,重传可能卡住 | 依赖 WebRTC 实现 |

**Phase 1 实现方案**: UDP 上增加应用层可靠性

**传输协议**:
- 所有消息(视频/音频/控制)都通过同一个 UDP Socket 发送
- 使用 RTP Payload Type 区分:
  - PT=96: H.264 视频流 (无序,丢包可跳)
  - PT=97: Opus 音频流 (无序,丢包可跳)
  - PT=98: 控制消息 (有序,需要 ACK)
- 控制消息包含序列号,接收端确认后发送 ACK
- 发送端定期重试未确认的消息 (最多 3 次,间隔 50ms)

**输入事件类型**:
```cpp
enum class InputEventType : uint8_t {
  kMouseMove = 0,     // 无需可靠(位置会被覆盖)
  kMouseClick = 1,    // 需要可靠(点击不能丢)
  kMouseWheel = 2,    // 需要可靠(滚轮操作不能丢)
  kKeyDown = 3,       // 需要可靠(按键不能丢)
  kKeyUp = 4,         // 需要可靠(释放键不能丢)
  kTouchEvent = 5,    // 可选(未来触屏支持)
};

struct InputEvent {
  InputEventType type;
  uint16_t x, y;           // 鼠标坐标
  uint8_t button;          // 1=左键, 2=右键, 3=中键
  uint8_t state;           // 0=抬起, 1=按下
  int16_t wheel_delta;     // 滚轮值
  uint32_t key_code;       // 虚拟按键码 (VK_* for Windows, NSEvent key for macOS)
  uint32_t modifier_keys;  // Ctrl/Shift/Alt/Cmd 标记位
};
```

**可靠性保证**:
1. **序列号**: 每个控制消息有独立的序列号 (不与媒体流混合)
2. **ACK 确认**: 接收端收到控制消息立即发送 ACK RTP 包 (PT=99)
3. **重试机制**: 发送端保留未确认消息队列,定期重试(最多 3 次,每次间隔 50ms)
4. **超时丢弃**: 重试 3 次后仍无 ACK,丢弃并记录 warning

**发送端代码示例**:
```cpp
class ReliableInputSender {
 private:
  struct PendingMessage {
    InputEvent event;
    uint16_t seq;
    uint64_t send_time_ms;
    int retry_count = 0;
    static constexpr int kMaxRetries = 3;
    static constexpr int kRetryTimeoutMs = 50;
  };

  void SendInputEvent(const InputEvent& event) {
    auto msg = PendingMessage{event, next_seq_++, now_ms(), 0};
    pending_.push(msg);
    SendViaRTP(event, msg.seq);
    ZENREMOTE_DEBUG("Input event sent: type={}, seq={}", event.type, msg.seq);
  }

  void ProcessRetries() {
    auto now_ms = GetCurrentTimeMs();
    std::queue<PendingMessage> remaining;
    while (!pending_.empty()) {
      auto msg = pending_.front();
      pending_.pop();
      
      if (now_ms - msg.send_time_ms >= msg.kRetryTimeoutMs) {
        if (msg.retry_count < msg.kMaxRetries) {
          msg.retry_count++;
          msg.send_time_ms = now_ms;
          SendViaRTP(msg.event, msg.seq);
          ZENREMOTE_WARN("Retrying input event: seq={}, attempt={}", 
                        msg.seq, msg.retry_count);
          remaining.push(msg);
        } else {
          ZENREMOTE_ERROR("Input event failed after {} retries: seq={}", 
                         msg.kMaxRetries, msg.seq);
          // 记录失败但不中断,继续处理后续事件
        }
      } else {
        remaining.push(msg);
      }
    }
    pending_ = remaining;
  }
};
```

**接收端代码示例**:
```cpp
class InputEventReceiver {
 public:
  void OnInputEvent(const InputEvent& event, uint16_t seq) {
    // 1. 直接应用输入(无缓冲,实时响应)
    ApplyInputEvent(event);
    
    // 2. 立即发送 ACK
    SendAck(seq);
    
    ZENREMOTE_DEBUG("Input event applied: type={}, seq={}", event.type, seq);
  }

 private:
  void ApplyInputEvent(const InputEvent& event) {
    switch (event.type) {
      case InputEventType::kMouseMove:
        SetMousePosition(event.x, event.y);  // Windows: SetCursorPos / macOS: CGWarpMouseCursorPosition
        break;
      case InputEventType::kMouseClick:
        if (event.state == 0)
          MouseUp(event.button);
        else
          MouseDown(event.button);
        break;
      case InputEventType::kKeyDown:
        KeyDown(event.key_code, event.modifier_keys);  // Windows: SendInput / macOS: CGEventPost
        break;
      case InputEventType::kKeyUp:
        KeyUp(event.key_code, event.modifier_keys);
        break;
      case InputEventType::kMouseWheel:
        MouseWheel(event.wheel_delta);
        break;
      default:
        ZENREMOTE_WARN("Unknown input event type: {}", (int)event.type);
    }
  }

  void SendAck(uint16_t seq) {
    // 构造 ACK RTP 包 (PT=99)
    RTPHeader hdr;
    hdr.payload_type = 99;
    hdr.sequence_number = ack_seq_++;
    hdr.timestamp = GetTimestamp();
    hdr.ssrc = my_ssrc_;
    hdr.marker = false;
    
    struct AckPayload {
      uint16_t acked_seq;
      uint32_t original_timestamp;  // 用于 RTT 计算
    } ack = {seq, 0};  // 可选填充时间戳
    
    SendViaUDP(RTPPacket{hdr, SerializeAck(ack)});
  }
};
```

**性能特性**:
- **输入延迟**: 鼠标/键盘事件 < 50ms (1 个 50ms 重试窗口内的最坏情况)
- **可靠性**: 99.9% 成功率 (局域网丢包率 < 0.1% 时)
- **CPU 开销**: < 0.1% (简单的序列号对比)
- **带宽开销**: ~50 bytes/事件 (包含 RTP 头 + 控制消息),事件频率 100Hz 时约 40 Kbps

**Platform-Specific 实现**:

**Windows**:
```cpp
// 捕获输入
WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEMOVE:
      OnMouseEvent({InputEventType::kMouseMove, GET_X(lp), GET_Y(lp), ...});
      break;
    case WM_KEYDOWN:
      OnKeyEvent({InputEventType::kKeyDown, wp, ...});
      break;
  }
}

// 注入输入
void InputEventReceiver::ApplyInputEvent(const InputEvent& event) {
  INPUT inputs[1] = {};
  if (event.type == InputEventType::kMouseMove) {
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = event.x;
    inputs[0].mi.dy = event.y;
    inputs[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  }
  SendInput(1, inputs, sizeof(INPUT));
}
```

**macOS**:
```cpp
// 捕获输入
func applicationDidFinishLaunching(_ aNotification: Notification) {
  let eventMask: NSEvent.EventTypeMask = [.mouseMoved, .leftMouseDown, ...]
  NSEvent.addLocalMonitorForEvents(matching: eventMask) { event in
    self.onInputEvent(type: event.type, ...)
    return event
  }
}

// 注入输入
func applyInputEvent(_ event: InputEvent) {
  let mouseEvent = CGEvent(mouseEventSource: nil,
                          mouseType: .mouseMoved,
                          mouseCursorPosition: CGPoint(x: event.x, y: event.y),
                          modifierFlags: CGEventFlags(rawValue: event.modifier_keys))
  mouseEvent?.post(tap: .cgSessionEventTap)
}
```

**优势总结**:
- ✅ **低延迟**: 无连接建立开销,实时传输
- ✅ **简单可靠**: UDP + 应用层 ACK 足以应对局域网高丢包率(<0.1%)
- ✅ **高效集成**: 视频/音频/控制共享同一 Socket,无需多连接管理
- ✅ **可扩展性**: Phase 2 可升级为 GCC 拥塞控制 + NACK 重传,无需改变基础协议
- ✅ **调试友好**: 所有包通过 Wireshark 可见,便于网络诊断

---

### 1.3 传输协议决策分析

#### **为什么选择 UDP + 应用层可靠性?**

**对比方案**:
| 对比维度 | UDP + App重试 | TCP | WebRTC DataChannel |
|---------|---------------|-----|------------------|
| **可靠性** | 99.9% (3次重试) | 100% | 100% |
| **P2P 延迟** | 0-5ms (正常) | 10-50ms (slow start) | 50-500ms (库开销) |
| **建立时间** | 0ms (无连接) | 100-300ms (3-way) | 1-5s (ICE+DTLS) |
| **多路复用** | ✅ 原生支持 | ❌ 需独立 TCP | ❌ 独立 channel |
| **代码复杂度** | 简单 (~500 lines) | 中等 | 极复杂 (20k+ lines) |
| **阻塞行为** | 非阻塞 (丢包不卡) | **可能阻塞** | 非阻塞 |
| **局域网适配** | 完美 (丢包<0.1%) | 过度设计 | 严重过度设计 |
| **跨平台开发** | ✅ 标准 Socket | ✅ 标准 | ❌ WebRTC 库依赖 |

**关键决策**:

1. **为什么不用 TCP?**
   ```
   问题: TCP 为了保证可靠性,如果丢包会自动重传
   
   场景: 鼠标输入丢包
   TCP 行为:
     ├─ 鼠标事件丢包 → 触发重传定时器
     ├─ 等待 ACK (通常 50-200ms)
     ├─ 控制消息被延迟
     ├─ 用户感受: 鼠标卡顿!
   
   UDP + App层:
     ├─ 鼠标事件丢包 → 立即重试 (50ms)
     ├─ 如果重试成功,延迟仅 50ms
     ├─ 如果再丢包,再重试 (99.9% 最终送达)
     ├─ 视频/音频不受影响 (不阻塞)
     ├─ 用户感受: 输入流畅!
   
   数学证明:
   - TCP 最坏延迟: 200ms (重传超时)
   - UDP + 3 次重试: 最坏 150ms (3×50ms)
   - ✅ UDP 更快,且不阻塞其他流
   ```

2. **为什么不用 WebRTC DataChannel?**
   ```
   DataChannel 基于 SCTP 协议,需要:
   
   Phase 1 成本:
   ├─ WebRTC 库编译: 200-500MB
   ├─ 完整 DTLS/SRTP: 用不到 (局域网)
   ├─ ICE 候选地址: 用不到 (手动输入 IP)
   ├─ 会话建立: 2-5 秒 (远高于 UDP 的 0ms)
   
   问题:
   ├─ 过度设计 (远程桌面不需要公网穿透)
   ├─ 锁定 WebRTC (难以自定义编码器)
   ├─ 调试困难 (DTLS 加密,无法用 Wireshark)
   ├─ 依赖维护 (需要跟踪 WebRTC 更新)
   
   相比 UDP + App层:
   └─ ✅ 代码 20 倍精简
   └─ ✅ 延迟 10 倍低
   └─ ✅ 编译体积 1000 倍小
   ```

3. **为什么选择 UDP?**
   ```
   关键优势:
   ✅ 完全控制延迟 (无 TCP slow start, 无 WebRTC 库开销)
   ✅ 与媒体流共享 Socket (视频 + 音频 + 控制 = 同一 UDP)
   ✅ 应用层可靠性足够 (局域网丢包 < 0.1%)
   ✅ 便于诊断 (Wireshark 可见所有包)
   ✅ Phase 2 平滑升级 (仅添加拥塞控制,无需改协议)
   ✅ 跨平台标准 (Windows/macOS/Linux 统一 Socket API)
   ```

**验证对比 (场景模拟)**:

**场景 A: 完美网络 (0% 丢包)**
```
三种方案表现相近:
- UDP + App:    延迟 5ms,吞吐量 2Mbps
- TCP:          延迟 10-20ms,吞吐量 2Mbps (slow start)
- DataChannel:  延迟 100-200ms,吞吐量 1Mbps (库开销)

✅ 胜者: UDP (最低延迟,立即可用)
```

**场景 B: 轻微抖动 (1% 丢包)**
```
模拟 100 个鼠标事件/秒:

UDP + App (3次重试):
  成功率 = 1 - (0.01)^3 = 99.9%
  延迟 99.9%: 5ms | 0.1%: 50-150ms
  平均: ~5ms + ε

TCP:
  丢包重传触发: 平均延迟 = 5 + 0.01×50 = 5.5ms
  但峰值 50-200ms (重传超时)
  问题: ❌ 偶发高延迟,用户感受"卡顿"

DataChannel:
  SCTP 重传: 延迟 100-200ms
  问题: ❌ 始终高延迟

✅ 胜者: UDP (低延迟,无峰值)
```

**场景 C: 视频丢包 (需跳帧)**
```
假设 H.264 I 帧 (300KB) 分 20 个 RTP 包

UDP + App:
  ├─ 第 5 个包丢失 → 整帧丢弃
  ├─ 下一个 I 帧到达 → 恢复解码
  ├─ 总延迟增加: ~33ms (1 帧)

TCP:
  ├─ 第 5 个包丢失 → TCP 等待重传
  ├─ 延迟增加: 50-200ms (重传超时)
  ├─ 整个视频暂停 (所有包阻塞)
  ├─ 用户感受: ❌ 视频卡死

✅ 胜者: UDP (视频继续,仅丢弃一帧)
```

**结论**: UDP + 应用层可靠性是最优平衡点
- 延迟: UDP (5ms) << TCP (20ms) << DataChannel (200ms)
- 可靠性: 都支持,UDP 更轻量
- 复杂度: UDP (简单) << TCP (中等) << DataChannel (复杂)
- 可维护性: UDP (高) >> DataChannel (低,依赖库)

---

#### **模块 9: 渲染与播放 (Rendering & Playback)**
- **视频渲染**: 复用你 zenremote 的渲染管线(OpenGL/D3D11)
- **音频播放**: SDL2 Audio 或 PortAudio

---

## 二、参考 WebRTC 的核心模块

### 2.1 Phase 1 必须实现的模块

| WebRTC 模块 | 功能 | Phase 1 实现方式 |
|------------|------|----------------|
| **RTP** | 媒体传输封装 | 简化版 RTP 头(序列号+时间戳+SSRC) |
| **Video Codec** | H.264 编解码 | FFmpeg (硬件加速优先) |
| **Audio Codec** | Opus 编解码 | FFmpeg Opus 编码器 |
| **Jitter Buffer** | 抗网络抖动 | 参考 WebRTC 动态缓冲区 |
| **Pacer** | 平滑发送 | 参考 WebRTC PacedSender 机制 |

### 2.2 Phase 2 添加的模块(无 STUN/ICE)

| 模块 | 功能 | Phase 2 实现方式 |
|------|------|----------------|
| **TURN** | 流量中继 | 自建 coturn 服务器(固定凭据) |
| **Signaling** | 连接/角色交换 | WebSocket + JSON 消息(无 SDP/ICE) |
| **Transport Security** | 传输加密 | OpenSSL/DTLS(可选,后期添加) |
| **Bandwidth Estimation** | 带宽估计 | 简化版 GCC 算法 |

### 2.2 可选参考的模块

- **Simulcast**: 多码率自适应(复杂,后期考虑)
- **SVC (可伸缩编码)**: H.264 SVC(编码器支持有限)
- **DataChannel**: 非媒体数据传输(如文件传输)

---

## 三、技术栈选型

### 3.1 编程语言
- **核心逻辑**: C++17 (与 zenremote 保持一致)
- **信令服务器**: C++ 或 Go (高并发)

### 3.2 第三方库

#### **Phase 1 依赖 (局域网版本)**

| 库名 | 用途 | 许可证 | 必要性 |
|-----|------|--------|-------|
| **FFmpeg** | 音视频编解码 | LGPL/GPL | 必须 |
| **SDL2** | 音频播放/窗口管理 | zlib | 必须 |
| **spdlog** | 日志 | MIT | 推荐 |
| **nlohmann/json** | 配置文件解析 | MIT | 推荐 |
| **Qt6** | UI 框架(可选) | LGPL | 可选 |

#### **Phase 2 新增依赖 (公网版本)**

| 库名 | 用途 | 许可证 |
|-----|------|--------|
| **libnice** | ICE/STUN 客户端 | LGPL/MPL |
| **OpenSSL** | DTLS 加密(可选) | Apache 2.0 |
| **libwebsockets** | WebSocket 客户端 | MIT |

### 3.3 构建系统
- **CMake**: 跨平台构建
- **Conan**: 依赖管理

---

## 四、P2P 网络测试方案

### 4.1 本地测试(同一局域网)

**场景 1: 直连测试**
- Windows 与 Mac 在同一 WiFi/路由器下
- 无需 STUN/TURN,直接通过局域网 IP 建立连接
- **优点**: 简单,延迟低
- **缺点**: 无法测试 NAT 穿透

**测试步骤**:
1. 关闭防火墙或开放 UDP 端口(如 50000-50010)
2. 硬编码对方 IP,跳过信令服务器
3. 验证音视频传输和控制指令

---

### 4.2 模拟 NAT 环境

**方案 1: 虚拟机 + NAT 网络**
- 在一台设备上运行多个虚拟机(VirtualBox/VMware)
- 配置不同类型的 NAT(Full Cone/Symmetric NAT)
- **优点**: 免费,可控
- **缺点**: 性能开销大

**方案 2: 软路由 + NAT 配置**
- 使用树莓派/软路由搭建测试网络
- 配置不同 NAT 类型进行测试

---

### 4.3 公网测试

**方案 1: 云服务器(推荐)**
- **TURN 服务器**: 部署在阿里云/AWS EC2 上
- **成本**: 
  - 轻量应用服务器: ¥24/月(2C2G,国内)
  - 按流量计费: ¥0.8/GB
- **优点**: 真实 NAT 环境,低成本
- **配置**:
  ```bash
  # 部署 coturn (开源 TURN 服务器)
  sudo apt install coturn
  sudo systemctl enable coturn
  ```

**方案 2: 云电脑(不推荐)**
- 成本高(¥100-200/月)
- 网络配置受限
- **结论**: 不如直接租云服务器

**方案 3: 使用 Tailscale/ZeroTier**
- 构建虚拟局域网,跨公网直连
- **优点**: 简单,免费
- **缺点**: 不是真正的 P2P,无法测试 TURN

---

### 4.4 测试矩阵

| 测试场景 | 端点 A | 端点 B | 网络环境 | 测试目标 |
|---------|--------|--------|---------|---------|
| 场景 1 | A 发起控制 | B 被控制 | 同局域网 | 基础功能 |
| 场景 2 | B 发起控制 | A 被控制 | 同局域网 | 角色互换 |
| 场景 3 | A (WiFi) | B (4G) | 公网 | TURN 中继 |
| 场景 4 | A (4G) | B (WiFi) | 公网 | 双向测试 |

---

## 五、开发策略

### 5.1 Phase 1: 局域网直连版本 (8-10 周)

这是当前的主要目标,完成后即可在局域网内使用远程控制功能。

#### **Sprint 1: 基础连接与传输 (2 周)**
**目标**: 建立 UDP 连接,传输原始视频帧

- **Week 1**: 
  - Windows: 屏幕采集(DXGI Desktop Duplication)
  - 配置管理(读取 IP/端口)
  - UDP Socket 封装
- **Week 2**: 
  - Mac: 接收 UDP 数据包
  - 简单渲染(SDL2 窗口显示原始帧)
  - 握手协议(确认连接建立)

**里程碑**: Windows 采集的屏幕能在 Mac 上显示(未压缩,低帧率)

---

#### **Sprint 2: 视频编解码与流控 (3 周)**
**目标**: 集成 H.264 编码器,实现平滑低延迟传输

- **Week 3**: 
  - FFmpeg H.264 编码器集成
  - 硬件加速支持(NVENC/VideoToolbox)
  - 编码器参数调优(低延迟预设: tune=zerolatency)
  - **发送端 Pacer**: 实现基础的平滑发送(固定 5ms 间隔)
- **Week 4**: 
  - FFmpeg H.264 解码器集成
  - 渲染优化(SDL2 或 D3D11/OpenGL 纹理渲染)
  - **接收端 JitterBuffer**: 实现基础的缓冲与重排序(固定 50ms)
- **Week 5**: 
  - RTP 封包(序列号+时间戳+SSRC)
  - 丢包处理(跳过损坏的帧)
  - **音视频同步**: 基于 RTP 时间戳对齐音视频
  - 性能测试(目标 30fps @ 1080p, 延迟 < 100ms)

**里程碑**: 视频传输流畅,即使网络有轻微抖动也能稳定播放

---

#### **Sprint 3: 输入控制 (2 周)**
**目标**: 实现鼠标键盘的远程控制

- **Week 6**: 
  - Windows: 鼠标事件捕获
  - 控制消息协议设计
  - Mac: 鼠标事件注入(CGEventPost)
- **Week 7**: 
  - 键盘事件捕获与注入
  - 特殊按键支持(Ctrl+C, Alt+Tab 等)
  - 坐标转换(不同分辨率适配)

**里程碑**: 可以远程操作鼠标键盘,响应延迟 < 50ms

---

#### **Sprint 4: 音频与双向支持 (2 周)**
**目标**: 添加音频传输,支持双向控制

- **Week 8**: 
  - 音频采集(WASAPI/Core Audio)
  - Opus 编码器集成
  - 音频播放(SDL2 Audio)
- **Week 9**: 
  - Mac 作为控制端,Windows 作为被控端
  - 双向测试与调试
  - 角色切换机制

**里程碑**: 完整功能的局域网远程桌面,支持双向控制

---

#### **Sprint 5: 优化与稳定性 (1-2 周)**
**目标**: 性能优化,错误处理,长时稳定运行

- **Week 10**: 
  - 内存泄漏排查
  - 多线程优化(采集、编码、传输分离)
  - 断线重连机制
  - UI 改进(显示延迟、帧率、码率)

**里程碑**: Phase 1 完成,可投入实际使用

---

### 5.2 Phase 2: 公网支持版本 (4-6 周)

**前置条件**: Phase 1 已完成并稳定运行

#### **Sprint 6: 信令服务器 (1-2 周)**
- 开发 WebSocket 信令服务器(Go/Node.js)
- 客户端信令协议
- 连接信息交换(IP、端口)

#### **Sprint 7: TURN 中继 (2 周)**
- 部署 coturn 服务器
- TURN 客户端实现
- 自动切换直连/中继

#### **Sprint 8: ICE 简化实现 (1-2 周)**
- 候选地址收集(本地、公网)
- 连接性检查
- 优选最佳路径

**里程碑**: 支持公网远程控制,任何网络环境下都能连接

---

### 5.3 为什么分两个阶段?

| 方面 | 一次性实现 | 分阶段实现 |
|-----|-----------|----------|
| **复杂度** | 高,需同时处理网络+功能 | 低,先聚焦核心功能 |
| **调试难度** | 困难(网络问题和功能问题混杂) | 简单(局域网稳定,易排查) |
| **可用性** | 延迟 3-4 个月才能使用 | 2 个月即可局域网使用 |
| **测试成本** | 需要云服务器 | 无需额外成本 |
| **学习曲线** | 陡峭(ICE/STUN/TURN 复杂) | 平缓(逐步深入) |
| **风险** | 高(可能因网络问题卡住) | 低(核心功能优先) |

---

### 5.2 跨平台开发策略

**问题**: 需要同时开发 Windows 和 Mac 版本才能看到效果

**解决方案**:

**方案 1: 分层开发(推荐)**
```
┌─────────────────────────────────┐
│   平台无关层 (80% 代码)          │
│   - 网络传输                     │
│   - 编解码逻辑                   │
│   - 协议处理                     │
│   - 状态管理                     │
└─────────────────────────────────┘
         │               │
         ▼               ▼
┌─────────────┐   ┌─────────────┐
│ Windows 平台层│   │  macOS 平台层 │
│ - DXGI 采集  │   │ - SCStream   │
│ - SendInput  │   │ - CGEvent    │
└─────────────┘   └─────────────┘
```

**步骤**:
1. **第 1-2 周**: 在 Windows 上实现采集+发送(UDP 广播)
2. **第 3 周**: 在 Mac 上实现接收+渲染(监听 UDP)
3. **第 4 周**: Mac 实现采集,Windows 实现接收
4. **第 5 周**: 集成信令服务器,完整 P2P

**方案 2: Mock 层测试**
- 开发 Mock 采集器(读取视频文件模拟屏幕)
- 单平台完成 80% 逻辑,最后移植平台相关代码

**方案 3: Linux 作为第三方**
- 先在 Linux 虚拟机上实现一个简化版
- 采集 X11 屏幕(xlib/XCB)
- 降低开发门槛

---

### 5.3 时间分配建议

| 阶段 | Windows 开发 | macOS 开发 | 联调测试 |
|-----|------------|-----------|---------|
| Week 1-2 | 屏幕采集+发送 | - | - |
| Week 3 | - | 接收+渲染 | 局域网测试 |
| Week 4 | 接收+渲染 | 屏幕采集+发送 | 双向测试 |
| Week 5-6 | 编解码集成 | 编解码集成 | 性能测试 |
| Week 7-8 | 输入控制 | 输入控制 | 功能测试 |
| Week 9-12 | P2P 网络层 | P2P 网络层 | NAT 穿透测试 |

---

## 六、风险与挑战

### 6.1 技术风险

| 风险项 | 影响 | 缓解措施 |
|-------|------|---------|
| NAT 穿透失败率高 | 无法建立连接 | 部署 TURN 服务器,保证 100% 连通 |
| 编码延迟 | 控制响应慢 | 使用硬件编码器,优化参数 |
| 音视频不同步 | 用户体验差 | 复用 zenremote 的 AV Sync 机制 |
| 跨平台 API 差异 | 开发周期长 | 抽象平台层,Mock 测试 |

### 6.2 资源需求

- **开发时间**: 3-4 个月(全职)
- **服务器成本**: ¥30-50/月(信令+TURN)
- **测试设备**: Windows + Mac 足够,无需云电脑

---

## 七、最小可行产品 (MVP) 定义

### Phase 1 MVP (局域网版本)

**核心功能**:
1. ✅ 局域网 UDP 直连(手动配置 IP)
2. ✅ 屏幕实时传输(H.264, 30fps)
3. ✅ 鼠标控制(点击、移动、滚轮)
4. ✅ 键盘控制(文本输入、快捷键)
5. ⭕ 音频传输(可选,不影响核心体验)

**验收标准**:
- ✅ 连接延迟 < 5ms (局域网)
- ✅ 端到端延迟 < 100ms (采集→传输→渲染)
- ✅ 分辨率 1920x1080 @ 30fps
- ✅ 码率 2-3 Mbps (局域网带宽充足)
- ✅ 鼠标/键盘响应 < 50ms
- ✅ 连续运行 1 小时无崩溃

**不包含的功能**:
- ❌ 公网连接
- ❌ NAT 穿透
- ❌ 信令服务器
- ❌ 加密传输(局域网可选)
- ❌ 带宽自适应(局域网无需)

---

### Phase 2 MVP (公网版本)

**新增功能**:
1. ✅ 信令服务器(自动交换连接信息)
2. ✅ TURN 中继(保证 100% 连通)
3. ✅ 简化的 ICE(优先直连,失败则中继)

**验收标准**:
- ✅ 任意网络环境下都能连接
- ✅ 端到端延迟 < 200ms (通过 TURN)
- ✅ 带宽自适应(1-5 Mbps 动态调整)

---

## 八、参考资源

### 8.1 WebRTC 源码
- **官方仓库**: https://webrtc.googlesource.com/src
- **关键目录**:
  - `api/`: PeerConnection API
  - `pc/`: SDP/ICE 逻辑
  - `modules/rtp_rtcp/`: RTP/RTCP 实现
  - `modules/video_coding/`: 视频编解码
  - `modules/congestion_controller/`: 拥塞控制

### 8.2 开源项目
- **Parsec**: 低延迟远程桌面(商业,可参考架构)
- **Moonlight**: 游戏串流(开源,基于 NVIDIA GameStream)
- **RustDesk**: Rust 实现的远程桌面(开源,类似 TeamViewer)

### 8.3 技术文档
- RFC 5245: ICE
- RFC 5764: DTLS-SRTP
- RFC 3550: RTP/RTCP
- RFC 6716: Opus Audio Codec

---

## 九、关于测试环境的建议

### 针对你的问题:

**Q: 是否需要云电脑测试?**  
**A: 不需要**,理由:
- 云电脑成本高,网络延迟大
- 你的两台设备(Windows + Mac)已足够
- 建议租一台轻量云服务器(¥24/月)部署 TURN,覆盖公网场景

**Q: 同网络下如何测试 NAT?**  
**A: 三种方式**:
1. **虚拟机 + NAT**: 在 Mac 上运行 Windows VM,配置 NAT 网络
2. **软路由**: 树莓派 + OpenWrt,模拟不同 NAT 类型
3. **云服务器**: 一台设备连公网服务器,模拟跨网段

**Q: 必须同时开发两个平台?**  
**A: 分阶段开发**:
- 前 2 周只开发 Windows 端(发送方)
- 第 3 周开发 Mac 端(接收方)
- 联调后再交换角色

---

## 十、下一步行动 (Phase 1)

### 第 1 周: 项目搭建与屏幕采集

**Day 1-2: 项目初始化**
```bash

# 创建目录结构
mkdir -p src/{common,network,media/{capture,codec,renderer},control,ui}
mkdir -p config docs tests
```

**Day 3-4: Windows 屏幕采集**
- 实现 `ScreenCapturerWin` (DXGI Desktop Duplication)
- 测试采集性能(目标 60fps @ 1080p)
- 输出到本地文件验证

**Day 5-7: UDP 传输层**
- 实现 `DirectConnection` 类(UDP Socket 封装)
- 配置文件解析(IP/端口)
- Windows 采集 → UDP 发送(广播模式测试)

---

### 第 2 周: Mac 接收与渲染

**Day 8-10: UDP 接收**
- Mac 端接收 UDP 数据包
- 简单的帧重组(处理分片)

**Day 11-14: 渲染测试**
- SDL2 窗口创建
- 显示原始 RGB 帧(未压缩)
- 测试端到端延迟

**里程碑检查**: 能在 Mac 上实时看到 Windows 屏幕(低帧率可接受)

---

### 第 3-5 周: 编解码集成

详见 Sprint 2 规划(已在上文)

---

### 配置文件示例

**Windows 端配置** (`config/zenremote_controller.json`):
```json
{
  "mode": "controller",
  "connection": {
    "type": "direct",
    "remote_ip": "192.168.1.100",
    "remote_port": 50000,
    "local_port": 50001
  },
  "video": {
    "codec": "h264",
    "width": 1920,
    "height": 1080,
    "fps": 30,
    "bitrate": 2000000,
    "hw_accel": true
  },
  "audio": {
    "enabled": false
  }
}
```

**Mac 端配置** (`config/zenremote_controlled.json`):
```json
{
  "mode": "controlled",
  "connection": {
    "type": "direct",
    "listen_port": 50000
  },
  "video": {
    "codec": "h264"
  },
  "input": {
    "enabled": true
  }
}
```

---

## 附录: 代码结构

### Phase 1 代码结构 (局域网版本)

```
zenremote/
├── CMakeLists.txt
├── conanfile.py
├── README.md
├── config/
│   ├── zenremote_controller.json   # 控制端配置示例
│   └── zenremote_controlled.json   # 被控端配置示例
├── src/
│   ├── main.cpp
│   ├── common/
│   │   ├── result.h              # 复用 zenremote 的 Result<T>
│   │   ├── logger.h              # spdlog 封装
│   │   ├── config_manager.h      # JSON 配置读取
│   │   └── types.h               # 通用类型定义
│   ├── network/
│   │   ├── direct_connection.h   # UDP 直连实现
│   │   ├── direct_connection.cpp
│   │   ├── packet.h              # 数据包定义
│   │   └── protocol.h            # 握手/控制消息协议
│   ├── media/
│   │   ├── capture/
│   │   │   ├── screen_capturer.h       # 抽象接口
│   │   │   ├── screen_capturer_win.cpp # Windows DXGI 实现
│   │   │   └── screen_capturer_mac.mm  # macOS SCStream 实现
│   │   ├── codec/
│   │   │   ├── video_encoder.h
│   │   │   ├── video_encoder.cpp       # FFmpeg H.264 编码
│   │   │   ├── video_decoder.h
│   │   │   ├── video_decoder.cpp       # FFmpeg H.264 解码
│   │   │   ├── audio_encoder.h         # Opus 编码
│   │   │   └── audio_decoder.h         # Opus 解码
│   │   ├── sync/
│   │   │   ├── jitter_buffer.h         # 接收端抖动缓冲
│   │   │   ├── jitter_buffer.cpp       # 参考 WebRTC FrameBuffer
│   │   │   ├── pacer.h                 # 发送端平滑发送
│   │   │   └── pacer.cpp               # 参考 WebRTC PacedSender
│   │   └── renderer/
│   │       ├── video_renderer.h        # 抽象接口
│   │       └── sdl_renderer.cpp        # SDL2 渲染实现
│   ├── control/
│   │   ├── input_handler.h             # 抽象接口
│   │   ├── input_capturer_win.cpp      # Windows 事件捕获
│   │   ├── input_capturer_mac.mm       # macOS 事件捕获
│   │   ├── input_injector_win.cpp      # Windows SendInput
│   │   └── input_injector_mac.mm       # macOS CGEventPost
│   ├── core/
│   │   ├── controller_session.h        # 控制端会话管理
│   │   ├── controller_session.cpp
│   │   ├── controlled_session.h        # 被控端会话管理
│   │   └── controlled_session.cpp
│   └── ui/
│       ├── main_window.h               # 简单的 SDL2 窗口
│       └── main_window.cpp
├── tests/
│   ├── test_connection.cpp             # 网络连接测试
│   ├── test_codec.cpp                  # 编解码测试
│   └── test_e2e.cpp                    # 端到端延迟测试
└── docs/
    ├── architecture.md
    ├── phase1_api.md                   # Phase 1 API 文档
    └── build_instructions.md           # 构建说明
```

### Phase 2 新增结构 (公网版本)

**Phase 1 完成后再添加**:
```
src/
├── signaling/
│   ├── signaling_client.h       # WebSocket 客户端
│   └── signaling_protocol.h     # 信令消息定义
├── network/
│   ├── turn_connection.h        # TURN 中继实现
│   ├── ice_lite.h               # 简化的 ICE 实现
│   └── connection_manager.h     # 统一连接管理(直连/中继)
└── security/
    └── dtls_transport.h         # DTLS 加密(可选)
```

---

---

## 总结与建议

### 为什么先做局域网版本?

1. **快速验证核心技术**: 2 个月即可看到完整效果,增强信心
2. **降低调试难度**: 局域网稳定,排除网络干扰,专注功能实现
3. **实用价值**: 局域网版本已可满足很多场景(家庭/办公室内控制)
4. **逐步深入**: 先掌握编解码、渲染、控制,再学习网络穿透
5. **零成本启动**: 无需云服务器,用现有设备即可开发测试

### 技术复用

你的 zenremote 项目经验可以直接复用:
- ✅ **60%** 编解码逻辑 (FFmpeg 封装)
- ✅ **40%** 渲染管线 (OpenGL/D3D11,但远程桌面更简单)
- ✅ **80%** 多线程架构 (loki 任务队列)
- ✅ **100%** 错误处理 (Result<T> 模式)
- ✅ **100%** 日志系统 (spdlog)

**不能复用的部分**:
- ❌ AV Sync 机制 (播放器基于文件 PTS,远程桌面需要 JitterBuffer)
- ❌ Seek 功能 (实时流无 Seek)
- ❌ 播放控制 (无暂停/倍速等概念)

### 时间估算

| 阶段 | 工作量 | 时间(每天 4-6 小时) |
|-----|-------|-------------------|
| **Phase 1: 局域网版本** | 全功能远程桌面 | 8-10 周 |
| **Phase 2: 公网支持** | 添加 TURN/信令 | 4-6 周 |
| **总计** | 完整的远程桌面系统 | 3-4 个月 |

### 关键成功因素

1. ✅ **聚焦 Phase 1**: 不要被 NAT 穿透分散注意力
2. ✅ **增量开发**: 每周都有可运行的版本
3. ✅ **充分测试**: 每个 Sprint 结束都做功能验证
4. ✅ **性能优先**: 硬件编码器 + 多线程是低延迟的关键
5. ✅ **文档记录**: 记录遇到的问题和解决方案

### 下一步

1. **立即行动**: 创建 zenremote 仓库,搭建项目框架
2. **第一个里程碑**: 1 周内实现 Windows 采集 → UDP → Mac 显示
3. **保持简单**: Phase 1 不要引入任何 P2P 相关的库
4. **随时咨询**: 遇到技术问题随时找我讨论

---

**这是一个循序渐进、价值清晰的学习路线。Phase 1 完成后,你将拥有一个可用的局域网远程桌面工具,并深入理解视频编解码、网络传输、跨平台开发等核心技术。Phase 2 则会让你掌握 WebRTC 的网络穿透精髓。**

**预祝项目顺利!有任何问题随时沟通。** 🚀
