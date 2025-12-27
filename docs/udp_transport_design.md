# UDP 传输层设计文档 (Phase 1 + Phase 2 扩展)

**作者**: Architecture Team  
**日期**: 2024  
**目标**: 为 zenremote 项目设计可扩展的 UDP 传输层,支持局域网直连和公网 TURN 中继

---

## 一、概述

### 1.1 设计目标

1. **Phase 1 (局域网直连)**
   - 简单、低延迟的 UDP 直连
   - 固定配置(手动指定对端 IP/端口)
   - 最小化复杂性,快速实现
   - 目标延迟: < 5ms (LAN) + < 100ms (端到端)

2. **Phase 2 (公网扩展)**
   - 信令服务器(自动交换连接信息)
   - TURN 中继(公网穿透)
   - 简化的 ICE(候选地址选择)
   - 自适应拥塞控制(GCC 算法)
   - 目标延迟: < 200ms (通过 TURN)

3. **设计原则**
   - **分层**: 网络传输与协议层分离
   - **可扩展**: Phase 1 代码无需修改,Phase 2 通过继承/组合添加功能
   - **WebRTC 兼容**: 采用 RTP/RTCP 标准,便于未来与 WebRTC 互操作
   - **日志集成**: 使用 `ZENREMOTE_*` 宏,便于诊断
   - **错误恢复**: 优雅处理网络中断、丢包、超时

---

## 二、架构设计

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────┐
│  应用层 (Application)                              │
│  - ControllerSession / ControlledSession            │
│  - 视频/音频/控制消息处理                           │
└────────────────┬──────────────────────────────────┘
                 │
┌────────────────┴──────────────────────────────────┐
│  协议层 (Protocol)                                │
│  - RTP Packet (视频/音频)                        │
│  - RTCP Report (控制/反馈)                       │
│  - Signaling Messages (信令)                     │
└────────────────┬──────────────────────────────────┘
                 │
┌────────────────┴──────────────────────────────────┐
│  连接管理 (Connection Manager) - Phase 2          │
│  - 候选地址收集 (Local/Public/Relay)             │
│  - 连接性检查                                     │
│  - 自动切换 (Direct ↔ TURN)                       │
└────────────────┬──────────────────────────────────┘
                 │
┌────────────────┴──────────────────────────────────┐
│  传输层 (Transport)                               │
│  - DirectConnection (Phase 1)                     │
│  - TurnConnection (Phase 2)                       │
│  - BaseConnection (公共接口)                      │
└────────────────┬──────────────────────────────────┘
                 │
┌────────────────┴──────────────────────────────────┐
│  网络 I/O (Socket)                                │
│  - UDP Socket 封装                                │
│  - 多平台支持 (Windows/macOS/Linux)              │
└─────────────────────────────────────────────────────┘
```

### 2.2 关键组件交互

**关键改进**: 新增控制消息流程,通过同一 UDP Socket 传输

```
发送端流程:
┌──────────────────────────────────────────────────┐
│ 应用层: 视频帧 / 音频数据 / 控制消息              │
└────────────┬───────────────────────────────────┘
             │ (raw data)
             ▼
┌──────────────────────────────────────────────────┐
│ RTP 打包: 添加序列号、时间戳、SSRC               │
│ - 视频 (PT=96): RTP seq + timestamp (90kHz)     │
│ - 音频 (PT=97): RTP seq + timestamp (48kHz)     │
│ - 控制 (PT=98): RTP seq + ACK mechanism         │
└────────────┬───────────────────────────────────┘
             │ (RTP packet with PT)
             ▼
┌──────────────────────────────────────────────────┐
│ Pacer (流量调度): 平滑发送,避免突发               │
│ - Phase 1: 固定间隔 5ms                          │
│ - 控制消息: 立即发送 (无需等待)                 │
│ - Phase 2: 动态间隔(基于拥塞控制)               │
└────────────┬───────────────────────────────────┘
             │ (scheduled packet)
             ▼
┌──────────────────────────────────────────────────┐
│ 连接选择: Direct 或 TURN                         │
└────────────┬───────────────────────────────────┘
             │
             ▼
┌──────────────────────────────────────────────────┐
│ UDP Socket: 发送到网络                           │
│ (单一端口,多路复用)                             │
└──────────────────────────────────────────────────┘

接收端流程:
┌──────────────────────────────────────────────────┐
│ UDP Socket: 接收网络数据                         │
│ (单一端口,多路复用)                             │
└────────────┬───────────────────────────────────┘
             │ (raw packet)
             ▼
┌──────────────────────────────────────────────────┐
│ RTP 解析: 根据 PT 分发                           │
│ - PT=96 (视频): 提取序列号、时间戳、SSRC       │
│ - PT=97 (音频): 检测丢包、乱序                   │
│ - PT=98/99 (控制): 处理输入事件、ACK应答       │
└────────────┬───────────────────────────────────┘
             │ (valid RTP packet by PT)
             ▼
┌──────────────────────────────────────────────────┐
│ 流量分类处理:                                   │
│ - 媒体流 (视频/音频): JitterBuffer + 解码       │
│ - 控制流 (输入事件): 直接应用,无缓冲            │
│ - ACK 流: 更新重传队列                          │
└────────────┬───────────────────────────────────┘
             │
             ▼
┌──────────────────────────────────────────────────┐
│ 应用层: 解码渲染 / 播放 / 输入注入               │
└──────────────────────────────────────────────────┘
```

**关键优化**:
1. **单一 Socket**: 所有流共享,无多端口管理
2. **立即应用输入**: 鼠标/键盘无缓冲,实时响应(< 5ms)
3. **独立可靠性**: 控制消息有序+ACK,媒体流无序无缓冲
4. **自动切换**: 同一 RTPReceiver 根据 PT 分发不同处理逻辑

---

## 三、多路复用与会话管理

### 3.0 单一 UDP Socket 多路复用设计

**核心原理**: 所有媒体流和控制消息共享一个 UDP Socket,通过 RTP Payload Type 区分

```
┌────────────────────────────────────────────┐
│          单一 UDP Socket                    │
│       (一个 IP:端口 对)                    │
└────────────────┬──────────────────────────┘
                 │
        ┌────────┼────────┐
        │        │        │
        ▼        ▼        ▼
┌─────────────┐┌─────────────┐┌──────────────┐
│   视频 RTP  ││  音频 RTP   ││ 控制 RTP    │
│  PT = 96    ││ PT = 97     ││ PT = 98/99  │
└─────────────┘└─────────────┘└──────────────┘
        │        │        │
    H.264      Opus    输入事件/ACK
    帧数据     PCM     (序列号+ACK)
```

**Payload Type (PT) 分配**:
| PT | 用途 | 传输方式 | 处理 |
|-----|------|---------|------|
| 96 | H.264 视频 | 无序不可靠 | 丢包跳过,解码失败刷新 I 帧 |
| 97 | Opus 音频 | 无序不可靠 | 丢包静音,继续播放 |
| 98 | 输入事件 (鼠标/键盘) | UDP + ACK | 需要可靠性,使用序列号+重试 |
| 99 | ACK 确认 | 快速应答 | 小包,优先发送 |

**优势**:
1. **减少端口占用**: 仅需 1 个 UDP 端口,便于防火墙配置
2. **简化时钟同步**: 所有流基于同一时间戳时钟
3. **改善 Jitter**: 避免多个 Socket 导致的处理不均
4. **便于限流**: 单个 Socket 即可进行总带宽限制
5. **标准化**: 遵循 RFC 5245 (ICE) 和 RFC 3550 (RTP),便于未来与 WebRTC 互操作

### 3.1 DirectConnection 类 (UDP 套接字封装)

**目的**: 最小化 UDP 操作复杂性,提供统一接口

**关键特性**:
- 创建和管理 UDP Socket
- 发送和接收数据
- 基本错误处理
- 非阻塞 I/O (可选异步)

```cpp
class DirectConnection {
 public:
  struct Config {
    std::string local_ip;
    uint16_t local_port;
    std::string remote_ip;
    uint16_t remote_port;
    int socket_buffer_size = 1024 * 1024;  // 1MB
    int recv_timeout_ms = 1000;  // 接收超时
  };

  explicit DirectConnection(const Config& config);
  ~DirectConnection();

  // 初始化连接
  bool Open();
  void Close();
  bool IsOpen() const;

  // 发送接收
  bool SendPacket(const uint8_t* data, size_t length);
  bool RecvPacket(uint8_t* buffer, size_t& length, int timeout_ms = 1000);

  // 统计信息
  const Stats& GetStats() const;

 private:
  Config config_;
  int socket_fd_ = -1;
  Stats stats_;

  bool CreateSocket();
  bool BindSocket();
};

struct Stats {
  uint64_t bytes_sent = 0;
  uint64_t bytes_received = 0;
  uint64_t packets_sent = 0;
  uint64_t packets_received = 0;
  uint64_t packets_lost = 0;
  uint32_t rtt_ms = 0;  // 往返延迟
};
```

### 3.2 数据包格式定义

**Phase 1**: 简化版 RTP + 自定义控制消息

#### 3.2.1 视频/音频 RTP 包

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Synchronization Source (SSRC) Identifier            |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|            Payload (媒体数据或控制命令)                       |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**RTP Header 字段定义**:
- **V (Version)**: 2 bits, 值为 2
- **P (Padding)**: 1 bit, Phase 1 为 0
- **X (Extension)**: 1 bit, Phase 1 为 0
- **CC (CSRC Count)**: 4 bits, 贡献源数,Phase 1 为 0
- **M (Marker)**: 1 bit, 帧边界标记(最后一个 RTP 包为 1)
- **PT (Payload Type)**: 7 bits
  - 96: Video (H.264)
  - 97: Audio (Opus)
  - 98: Control Message
- **Sequence Number**: 16 bits, 递增计数
- **Timestamp**: 32 bits, 媒体时间戳(采样时间)
- **SSRC**: 32 bits, 同步源标识(控制端/被控端)

```cpp
struct RTPHeader {
  static constexpr size_t kHeaderSize = 12;  // bytes
  
  uint8_t version = 2;      // always 2
  bool padding = false;
  bool extension = false;
  uint8_t csrc_count = 0;
  bool marker = false;
  uint8_t payload_type = 0;  // 96=Video, 97=Audio, 98=Control
  uint16_t sequence_number = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;

  // 序列化到字节流
  bool Serialize(uint8_t* buffer, size_t length);
  // 从字节流解析
  bool Deserialize(const uint8_t* buffer, size_t length);
};
```

#### 3.2.2 控制消息类型

**握手阶段**:
```cpp
enum class ControlMessageType : uint8_t {
  kHandshakeRequest = 0,   // 握手请求
  kHandshakeResponse = 1,  // 握手响应
  kHeartbeat = 2,          // 心跳
  kInputEvent = 3,         // 鼠标/键盘事件 (可靠传输)
  kAck = 4,                // 确认(仅控制消息)
  kReportStats = 5,        // 统计信息(RTCP 简化版)
};

struct ControlMessage {
  ControlMessageType type;
  uint32_t timestamp;
  // payload 取决于 type
};
```

#### 3.2.3 控制消息传输策略

**关键设计决策: UDP 上增加可靠性层 (Reliable UDP)**

| 特性 | 媒体流 (视频/音频) | 控制消息 (鼠标/键盘) |
|-----|------------------|------------------|
| **协议** | UDP (无序不可靠) | UDP (但需可靠) |
| **丢包处理** | 跳过帧 | 重传 |
| **顺序** | 无要求 | 严格有序 |
| **延迟** | 低延迟优先 | 保证送达 |
| **传输模式** | 单向(发送端→接收端) | 双向(需 ACK) |

**为什么不用 TCP?**
- TCP 建立连接开销大 (3-way handshake)
- 局域网环境可靠性已很高
- 与媒体流使用同一 UDP Socket 可降低复杂度
- WebRTC 也采用 UDP + 应用层重传

**为什么不用 DataChannel?**
- DataChannel 是 WebRTC 概念,非标准化
- Phase 1 目标保持简单,避免引入过多库
- Phase 2 可选扩展 WebRTC 时再考虑

**实现方案: 序列号 + ACK + 重传**

```cpp
// 控制消息 RTP 包扩展
struct ControlMessageHeader {
  uint16_t sequence_number;  // 控制消息序列号 (独立于媒体流)
  uint32_t timestamp;        // 时间戳
  bool requires_ack;         // 标记此消息需要 ACK
  uint8_t reserved = 0;
};

// 输入事件 RTP 包载荷
enum class InputEventType : uint8_t {
  kMouseMove = 0,
  kMouseClick = 1,    // 包含按钮类型和按下/抬起
  kMouseWheel = 2,    // 包含方向和滚动量
  kKeyDown = 3,       // 包含按键码
  kKeyUp = 4,         // 包含按键码
  kTouchEvent = 5,    // 可选,用于触屏设备
};

struct InputEvent {
  InputEventType type;
  uint16_t x;         // 鼠标 X 坐标 (相对于屏幕)
  uint16_t y;         // 鼠标 Y 坐标
  uint8_t button;     // 鼠标按钮: 1=左键, 2=右键, 3=中键
  uint8_t state;      // 0=抬起, 1=按下 (for click/key)
  int16_t wheel_delta; // 滚轮值 (正=向上, 负=向下)
  uint32_t key_code;  // 虚拟按键码 (VK_* for Windows, NSEvent key for macOS)
  uint32_t modifier_keys;  // Ctrl/Shift/Alt/Cmd 标记
};

// ACK 消息
struct AckMessage {
  uint16_t acked_sequence_number;
  uint32_t timestamp;  // 原消息时间戳,用于计算延迟
};
```

**发送侧可靠性机制**:

```cpp
class ReliableInputSender {
 private:
  struct PendingMessage {
    ControlMessage msg;
    std::vector<uint8_t> payload;
    uint64_t send_time_ms;
    int retry_count = 0;
    static constexpr int kMaxRetries = 3;
    static constexpr int kRetryTimeoutMs = 50;  // 快速重试
  };

  std::queue<PendingMessage> pending_messages_;
  uint16_t next_sequence_number_ = 0;

  void Send(const InputEvent& event) {
    ControlMessage msg;
    msg.type = ControlMessageType::kInputEvent;
    msg.sequence_number = next_sequence_number_++;
    msg.requires_ack = true;
    
    PendingMessage pm;
    pm.msg = msg;
    pm.send_time_ms = GetCurrentTimeMs();
    pending_messages_.push(pm);
    
    SendMessageViaRTP(msg, event);
  }

  void OnAck(const AckMessage& ack) {
    // 匹配 sequence number,移除待确认消息
    while (!pending_messages_.empty()) {
      auto& front = pending_messages_.front();
      if (front.msg.sequence_number == ack.acked_sequence_number) {
        uint64_t rtt_ms = GetCurrentTimeMs() - front.send_time_ms;
        ZENREMOTE_DEBUG("Input ACK received, RTT: {}ms", rtt_ms);
        pending_messages_.pop();
        break;
      } else if (front.msg.sequence_number < ack.acked_sequence_number) {
        pending_messages_.pop();  // 跳过旧消息
      } else {
        break;
      }
    }
  }

  void RetryPendingMessages() {
    auto now_ms = GetCurrentTimeMs();
    for (auto& pm : pending_messages_) {
      if (now_ms - pm.send_time_ms > pm.kRetryTimeoutMs) {
        if (pm.retry_count < pm.kMaxRetries) {
          ZENREMOTE_WARN("Retrying input message, seq: {}, attempt: {}",
                        pm.msg.sequence_number, pm.retry_count + 1);
          SendMessageViaRTP(pm.msg, pm.payload);
          pm.retry_count++;
          pm.send_time_ms = now_ms;
        } else {
          ZENREMOTE_ERROR("Input message failed after {} retries", 
                         pm.kMaxRetries);
          // 可选:通知用户或降级处理
        }
      }
    }
  }
};
```

**接收侧处理**:

```cpp
class ReliableInputReceiver {
 private:
  uint16_t last_acked_sequence_ = 0;

  void OnInputEvent(const InputEvent& event, uint16_t seq) {
    // 直接处理输入事件 (无需缓冲,实时响应)
    ApplyInputEvent(event);
    
    // 立即发送 ACK
    SendAck(seq);
    last_acked_sequence_ = seq;
  }

  void SendAck(uint16_t seq) {
    AckMessage ack;
    ack.acked_sequence_number = seq;
    ack.timestamp = GetTimestamp();
    SendViaRTP(ControlMessageType::kAck, ack);
  }
};
```

### 3.3 握手协议 (3-way handshake)

**时序图**:
```
控制端                                    被控端
  │                                         │
  ├─────── HandshakeRequest ──────────────→│
  │        (SSRC, session_id)               │
  │        (支持的功能:输入控制,音频)      │
  │                                         │
  │←────── HandshakeResponse ──────────────┤
  │        (SSRC, session_id, ACK)          │
  │        (协商功能,确认状态)             │
  │                                         │
  ├─────── Heartbeat (ACK) ───────────────→│
  │        (确认,开始媒体传输)             │
  │                                         │
  │◄═══════════════ Connected ═════════════►│
  │                                         │
  │ 视频 RTP 包 + 音频 RTP 包 + 输入事件  │
  │ (均通过同一 UDP Socket,不同 PT)       │
  │                                         │
```

**握手消息格式**:
```cpp
struct HandshakePayload {
  uint32_t version = 1;              // 协议版本
  uint32_t session_id;               // 会话 ID (随机)
  uint32_t ssrc;                     // 发送者 SSRC
  uint8_t supported_codecs;          // 支持的编码器
  uint16_t capabilities_flags = 0;   // 功能标志
};

// capabilities_flags (位掩码):
// Bit 0: Audio Support
// Bit 1: Hardware Encoding Support
// Bit 2: Reliable Input Support (控制消息 ACK 机制)
// Bit 3: Touchscreen Support
// Bit 4-15: Reserved
```

**意义**: 握手阶段协商双端的能力,包括是否支持鼠标键盘事件的可靠传输。

### 3.4 RTP 包管理

**发送端**:
```cpp
class RTPSender {
 public:
  explicit RTPSender(uint32_t ssrc, DirectConnection* conn);

  // 发送视频帧
  bool SendVideoFrame(const VideoFrame& frame, 
                      uint32_t timestamp_90khz);
  
  // 发送音频包
  bool SendAudioPacket(const uint8_t* data, size_t length,
                       uint32_t timestamp_48khz);

  // 发送控制消息
  bool SendControlMessage(const ControlMessage& msg);

 private:
  uint32_t ssrc_;
  uint16_t sequence_number_ = 0;
  DirectConnection* connection_;
  
  RTPHeader BuildHeader(uint8_t payload_type, 
                       uint16_t seq, 
                       uint32_t ts,
                       bool marker);
};
```

**接收端**:
```cpp
class RTPReceiver {
 public:
  RTPReceiver();

  // 接收 RTP 包
  bool ReceivePacket(const uint8_t* buffer, size_t length,
                     ReceivedPacket& out);

  // 检测丢包 (根据 sequence number 间隔)
  std::vector<uint16_t> DetectMissingSequences(
      uint16_t prev_seq, uint16_t curr_seq);

  // 获取接收统计
  const RTPReceiverStats& GetStats() const;

 private:
  uint16_t last_sequence_number_ = 0;
  uint32_t expected_timestamp_ = 0;
  RTPReceiverStats stats_;
};

struct ReceivedPacket {
  RTPHeader header;
  std::vector<uint8_t> payload;
  uint64_t arrival_time_ns;  // 纳秒级时间戳
};
```

### 3.5 Pacer (Phase 1 简化版)

**目的**: 避免网络突发,平滑发送

**Phase 1 实现**: 固定间隔发送(5ms)

```cpp
class Pacer {
 public:
  // 配置参数
  struct Config {
    uint32_t pacing_interval_ms = 5;  // 固定间隔
    uint32_t max_packets_per_batch = 10;
  };

  explicit Pacer(const Config& config);

  // 检查是否可以发送
  bool CanSend();

  // 记录已发送
  void OnPacketSent();

  // 重置(e.g., 连接重启)
  void Reset();

 private:
  Config config_;
  std::chrono::steady_clock::time_point last_send_time_;
  int packets_in_batch_ = 0;
};
```

### 3.6 JitterBuffer (Phase 1 简化版)

**目的**: 处理乱序和延迟

**Phase 1 实现**: 固定 50ms 缓冲

```cpp
class JitterBuffer {
 public:
  struct Config {
    uint32_t buffer_ms = 50;  // 固定缓冲深度
    uint32_t max_packets = 100;
  };

  explicit JitterBuffer(const Config& config);

  // 插入 RTP 包
  void InsertPacket(const ReceivedPacket& packet);

  // 尝试提取完整帧 (返回所有同一时间戳的包)
  bool TryExtractFrame(std::vector<uint8_t>& frame_data,
                       uint32_t& timestamp);

  // 获取当前缓冲时间
  uint32_t GetBufferedMs() const;

 private:
  Config config_;
  std::map<uint32_t, std::vector<ReceivedPacket>> buffer_;  // timestamp → packets
  uint32_t expected_timestamp_ = 0;
};
```

### 3.7 错误处理与恢复

**场景处理**:

| 场景 | 处理方式 | 备注 |
|------|---------|------|
| **视频包丢失** | 跳过(丢弃该帧) | 视频/音频,实时性优先 |
| **音频包丢失** | 丢包,继续播放 | 声音会中断但不卡 |
| **控制消息丢失** | 重试 3 次 | 保证鼠标键盘送达 |
| **乱序** | JitterBuffer 重排 | 自动处理 |
| **连接超时** | 自动重连 | 重新握手 |
| **无效 RTP 包** | 检查校验,丢弃 | 记录 warning 日志 |
| **缓冲溢出** | 清空旧帧,继续接收 | 极端网络条件 |
| **Socket 错误** | 关闭连接,标记失败 | 上层重试 |

**控制消息可靠性详细流程**:

```
发送端                          接收端
  │                              │
  ├─ 输入事件 (seq=1) ─────────→│
  │  (启动 50ms 重试计时器)       │ OnInputEvent(1)
  │                              ├─ 应用事件
  │                          ←─ ACK (seq=1) ─┤
  │  (收到 ACK,停止计时)         │ (立即响应)
  │                              │
  ├─ 输入事件 (seq=2) ─────────→│
  │  (丢包!)                      ✗
  │  (50ms 后重试)                │
  │                              │
  ├─ 输入事件 (seq=2) ──────────→│
  │  (第二次尝试)                 │ OnInputEvent(2)
  │  (重新启动计时器)             ├─ 应用事件
  │                          ←─ ACK (seq=2) ─┤
  │  (收到 ACK,停止)              │
  │                              │
  └─ ... 继续 ─────────────────→│
```

**统计指标**:
- 成功率: 99.9% (3 次重试覆盖 99.7% 的丢包)
- 延迟增加: 最坏情况 100ms (3×50ms 重试间隔)
- CPU 开销: < 0.1% (简单队列管理)

**代码示例**:
```cpp
bool DirectConnection::RecvPacket(uint8_t* buffer, 
                                   size_t& length, 
                                   int timeout_ms) {
  // 非阻塞接收
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  
  setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, 
             (const char*)&tv, sizeof(tv));
  
  int n = recvfrom(socket_fd_, buffer, length, 0, nullptr, nullptr);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ZENREMOTE_DEBUG("Socket receive timeout");
      return false;
    }
    ZENREMOTE_ERROR("Socket error: {}", strerror(errno));
    return false;
  }
  
  length = n;
  stats_.bytes_received += n;
  stats_.packets_received++;
  return true;
}
```

---

## 四、Phase 2 设计 (公网扩展)

### 4.1 连接管理器 (ConnectionManager)

**目的**: 抽象 Phase 1 和 Phase 2 的差异,统一接口

```cpp
// 基类 (Phase 1 + Phase 2 共用)
class BaseConnection {
 public:
  virtual ~BaseConnection() = default;

  virtual bool Open() = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;

  virtual bool Send(const uint8_t* data, size_t length) = 0;
  virtual bool Recv(uint8_t* buffer, size_t& length, 
                    int timeout_ms = 1000) = 0;

  // Phase 2: 获取连接属性
  virtual ConnectionType GetType() const = 0;
  virtual int GetRTT() const = 0;  // 往返延迟 (毫秒)
};

enum class ConnectionType {
  kDirect,   // 直连 (Phase 1/2)
  kRelay,    // TURN 中继 (Phase 2)
};

// Phase 1 实现 (已有)
class DirectConnection : public BaseConnection { /* ... */ };

// Phase 2: TURN 中继实现
class TurnConnection : public BaseConnection {
 public:
  struct Config {
    std::string turn_server_ip;
    uint16_t turn_server_port;
    std::string username;
    std::string password;
    // ... TURN 认证参数
  };
  
  explicit TurnConnection(const Config& config);
  
  bool Open() override;
  void Close() override;
  // ... 其他接口
};

// 连接选择器 (Phase 2)
class ConnectionManager {
 public:
  struct CandidateAddress {
    std::string address;
    uint16_t port;
    ConnectionType type;
    int priority;  // 优先级: 直连 > 中继
  };

  // 收集候选地址 (ICE Lite)
  void GatherCandidates();

  // 尝试连接 (按优先级)
  bool ConnectToBest();

  // 获取当前连接
  BaseConnection* GetActiveConnection();

  // 定期健康检查
  void HealthCheck();

 private:
  std::vector<CandidateAddress> candidates_;
  std::unique_ptr<BaseConnection> active_connection_;
};
```

### 4.2 信令服务器协议 (Phase 2)

**会话交换格式** (WebSocket JSON):

```json
// 1. 发起方注册
{
  "type": "register",
  "session_id": "uuid-12345",
  "role": "controller",
  "local_candidates": [
    {"ip": "192.168.1.100", "port": 50000, "type": "host"},
    {"ip": "8.8.8.8", "port": 50000, "type": "srflx"}
  ]
}

// 2. 信令服务器返回对端信息
{
  "type": "peer_info",
  "session_id": "uuid-12345",
  "peer_candidates": [
    {"ip": "192.168.1.101", "port": 50001, "type": "host"},
    {"ip": "1.1.1.1", "port": 50001, "type": "srflx"}
  ],
  "remote_ssrc": 0x87654321
}

// 3. 定期心跳
{
  "type": "heartbeat",
  "session_id": "uuid-12345"
}
```

### 4.3 TURN 协议集成 (Phase 2)

使用开源 libnice 或 coturn 客户端库

**流程**:
1. 连接 TURN 服务器
2. 发送 Allocate 请求,获得中继地址
3. 通过信令服务器交换 TURN 地址
4. 对端连接到 TURN 中继
5. 媒体通过 TURN 服务器转发

```cpp
class TurnConnection : public BaseConnection {
 private:
  struct {
    std::string relay_address;
    uint16_t relay_port;
    std::string lifetime_token;
  } turn_allocation_;

  bool AllocateRelay();
  bool RefreshAllocation();
};
```

### 4.4 拥塞控制 (Phase 2)

**GCC 算法 (Google Congestion Control)**:

```cpp
class CongestionController {
 public:
  struct Config {
    uint32_t min_bitrate_bps = 200000;      // 200 kbps
    uint32_t max_bitrate_bps = 10000000;    // 10 Mbps
    uint32_t start_bitrate_bps = 2500000;   // 2.5 Mbps
  };

  // 报告丢包率
  void OnPacketLoss(double loss_ratio);

  // 报告延迟增长
  void OnDelayIncrease(int delay_increase_ms);

  // 获取当前目标码率
  uint32_t GetTargetBitrateBps() const;

  // 定期调用 (每 100ms)
  void Update();

 private:
  Config config_;
  uint32_t current_bitrate_bps_;
  
  // 基于延迟的估计
  uint32_t EstimateBytesByDelay();
  // 基于丢包的估计
  uint32_t EstimateByLoss();
};
```

### 4.5 自适应 JitterBuffer (Phase 2)

**动态缓冲计算**:

```cpp
class AdaptiveJitterBuffer : public JitterBuffer {
 private:
  // 基础延迟 (最小观测)
  uint32_t base_delay_ms_ = 0;

  // 抖动值 (RFC 3550)
  uint32_t jitter_ms_ = 0;

  // 目标缓冲 = base_delay + 4 * jitter (WebRTC 推荐)
  uint32_t CalculateTargetBuffer() {
    return base_delay_ms_ + 4 * jitter_ms_;
  }

  // 抖动更新 (RFC 3550 § A.8)
  void UpdateJitter(int32_t delay_ms) {
    int32_t delta = delay_ms - expected_delay_ms_;
    jitter_ms_ = (15 * jitter_ms_ + abs(delta)) / 16;
  }
};
```

### 4.6 NACK 重传 (Phase 2)

**选择性重传关键帧**:

```cpp
class NACKHandler {
 public:
  // 检测丢包后发送 NACK
  void OnMissingSequence(uint16_t seq_start, uint16_t seq_end);

  // 重传队列
  void EnqueueForRetransmit(const RTPPacket& pkt);

  // Pacer 中优先调度重传包
  RTPPacket* GetNextPacketToSend();

 private:
  std::queue<RTPPacket> retransmit_queue_;
  std::set<uint16_t> acked_sequences_;
};
```

---

## 五、集成指南

### 5.1 应用层接口

**Phase 1**:
```cpp
// 控制端(发送屏幕 + 接收输入)
class ControllerSession {
 private:
  std::unique_ptr<DirectConnection> connection_;
  RTPSender video_sender_;
  ReliableInputReceiver input_receiver_;
  Pacer pacer_;

  void SendVideoFrame(const VideoFrame& frame) {
    if (pacer_.CanSend()) {
      video_sender_.SendVideoFrame(frame, GetTimestamp());
      pacer_.OnPacketSent();
    }
  }

  void ReceiveInputAndMediaLoop() {
    uint8_t buffer[65536];
    size_t length;
    
    if (connection_->RecvPacket(buffer, length)) {
      ReceivedPacket pkt;
      if (receiver_.ReceivePacket(buffer, length, pkt)) {
        // 根据 Payload Type 分发
        switch (pkt.header.payload_type) {
          case 98:  // Control Message
            input_receiver_.OnControlMessage(pkt.payload);
            break;
          default:
            break;
        }
      }
    }
  }
};

// 被控端(接收屏幕 + 发送输入)
class ControlledSession {
 private:
  std::unique_ptr<DirectConnection> connection_;
  RTPReceiver receiver_;
  JitterBuffer jitter_buffer_;
  ReliableInputSender input_sender_;

  void ReceiveMediaAndSendInputLoop() {
    // 接收媒体
    uint8_t buffer[65536];
    size_t length;
    
    if (connection_->RecvPacket(buffer, length)) {
      ReceivedPacket pkt;
      if (receiver_.ReceivePacket(buffer, length, pkt)) {
        // 根据 Payload Type 分发
        switch (pkt.header.payload_type) {
          case 96:  // Video
            jitter_buffer_.InsertPacket(pkt);
            std::vector<uint8_t> frame;
            uint32_t timestamp;
            if (jitter_buffer_.TryExtractFrame(frame, timestamp)) {
              OnVideoFrame(frame);  // 解码渲染
            }
            break;
          case 97:  // Audio
            OnAudioPacket(pkt.payload);
            break;
          case 4:   // ACK
            input_sender_.OnAck(pkt.payload);
            break;
          default:
            break;
        }
      }
    }
  }

  void OnLocalMouseEvent(const MouseEvent& event) {
    InputEvent input;
    input.type = InputEventType::kMouseMove;
    input.x = event.x;
    input.y = event.y;
    input_sender_.Send(input);
  }

  void OnLocalKeyboardEvent(const KeyboardEvent& event) {
    InputEvent input;
    input.type = event.is_down ? InputEventType::kKeyDown : InputEventType::kKeyUp;
    input.key_code = event.key_code;
    input.modifier_keys = event.modifiers;
    input_sender_.Send(input);
  }
};
```

**关键特性**:
1. **同一 UDP Socket 多路复用**: 视频(PT=96) + 音频(PT=97) + 控制(PT=98) + ACK(PT=99)
2. **输入事件可靠传输**: 序列号 + ACK 重传机制
3. **低延迟**: 输入事件无需缓冲,即收即处理
4. **双向通信**: 原生支持角色互换

### 5.1.1 多路复用接收循环示例

```cpp
// 被控端的统一接收循环 (处理视频/音频/控制)
class MultiplexReceiver {
 private:
  std::unique_ptr<DirectConnection> connection_;
  RTPReceiver rtp_receiver_;
  JitterBuffer video_buffer_;
  ReliableInputReceiver input_receiver_;

  void ReceiveLoop() {
    uint8_t buffer[65536];
    while (is_running_) {
      size_t length = sizeof(buffer);
      
      // 1. 接收 UDP 包
      if (!connection_->RecvPacket(buffer, length)) {
        continue;
      }

      // 2. 解析 RTP 头
      ReceivedPacket pkt;
      if (!rtp_receiver_.ReceivePacket(buffer, length, pkt)) {
        ZENREMOTE_WARN("Invalid RTP packet");
        continue;
      }

      // 3. 根据 Payload Type 分发处理
      switch (pkt.header.payload_type) {
        case 96:  // 视频 (H.264)
          HandleVideoPacket(pkt);
          break;
        
        case 97:  // 音频 (Opus)
          HandleAudioPacket(pkt);
          break;
        
        case 98:  // 输入事件 (鼠标/键盘)
          HandleInputEvent(pkt);
          break;
        
        case 99:  // ACK 确认
          HandleAckMessage(pkt);
          break;
        
        default:
          ZENREMOTE_WARN("Unknown payload type: {}", pkt.header.payload_type);
      }
    }
  }

  void HandleVideoPacket(const ReceivedPacket& pkt) {
    // 插入 JitterBuffer
    video_buffer_.InsertPacket(pkt);
    
    // 尝试提取完整帧
    std::vector<uint8_t> frame_data;
    uint32_t timestamp;
    if (video_buffer_.TryExtractFrame(frame_data, timestamp)) {
      // 解码渲染
      VideoFrame frame = video_decoder_.Decode(frame_data);
      renderer_.RenderFrame(frame);
      ZENREMOTE_DEBUG("Video frame rendered: timestamp={}", timestamp);
    }
  }

  void HandleAudioPacket(const ReceivedPacket& pkt) {
    // 音频直接播放,无缓冲(或最小缓冲)
    std::vector<int16_t> pcm_data = audio_decoder_.Decode(pkt.payload);
    audio_player_.PlaySamples(pcm_data);
    ZENREMOTE_DEBUG("Audio packet played: {} samples", pcm_data.size());
  }

  void HandleInputEvent(const ReceivedPacket& pkt) {
    // 解析输入事件
    InputEvent event = ParseInputEvent(pkt.payload);
    uint16_t seq = ExtractSequenceNumber(pkt.payload);
    
    // 立即应用输入,无缓冲
    input_receiver_.OnInputEvent(event, seq);
    ZENREMOTE_DEBUG("Input event applied: type={}, seq={}", 
                    (int)event.type, seq);
  }

  void HandleAckMessage(const ReceivedPacket& pkt) {
    // 解析 ACK
    AckMessage ack = ParseAckMessage(pkt.payload);
    input_sender_.OnAckMessage(ack);
    ZENREMOTE_DEBUG("ACK received: seq={}", ack.acked_sequence_number);
  }
};
```

**Phase 2** (无需改 Phase 1 代码):
```cpp
// 仅在初始化时替换连接
std::unique_ptr<BaseConnection> connection;

if (config.connection_type == "direct") {
  connection = std::make_unique<DirectConnection>(...);
} else if (config.connection_type == "turn") {
  connection = std::make_unique<ConnectionManager>(...);
  // 内部自动尝试直连和 TURN 中继
}

// RTPSender/RTPReceiver/Pacer/JitterBuffer 无需改动
video_sender_ = RTPSender(ssrc, connection.get());
```

### 5.2 配置文件扩展

**Phase 1 配置** (`config/zenremote.json`):
```json
{
  "network": {
    "mode": "direct",
    "local_ip": "0.0.0.0",
    "local_port": 50000,
    "remote_ip": "192.168.1.100",
    "remote_port": 50000
  },
  "transport": {
    "pacer_interval_ms": 5,
    "jitter_buffer_ms": 50,
    "socket_buffer_size": 1048576
  }
}
```

**Phase 2 配置** (添加):
```json
{
  "network": {
    "mode": "auto",  // direct -> turn fallback
    "signaling_server": "wss://signal.zenremote.com",
    "turn_servers": [
      {
        "urls": "turn:turn.zenremote.com:3478",
        "username": "user@zenremote.com",
        "credential": "password"
      }
    ]
  },
  "congestion": {
    "enable_gcc": true,
    "min_bitrate_bps": 200000,
    "max_bitrate_bps": 10000000
  }
}
```

---

## 六、CMakeLists.txt 集成

### 6.1 源文件组织

```cmake
# src/network/CMakeLists.txt
add_library(zenremote_network
  # Phase 1 核心
  direct_connection.cpp
  rtp_sender.cpp
  rtp_receiver.cpp
  pacer.cpp
  jitter_buffer.cpp
  
  # Phase 2 (可选)
  connection_manager.cpp
  turn_connection.cpp
  congestion_controller.cpp
  nack_handler.cpp
)

target_include_directories(zenremote_network PUBLIC
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(zenremote_network PUBLIC
  zenremote_common
  spdlog::spdlog
)

# 条件编译 Phase 2 功能
if(ENABLE_PHASE2)
  target_compile_definitions(zenremote_network PRIVATE
    ZENREMOTE_PHASE2_ENABLED
  )
endif()
```

### 6.2 单元测试

```cmake
# tests/CMakeLists.txt
add_executable(test_network
  test_rtp_packet.cpp
  test_pacer.cpp
  test_jitter_buffer.cpp
)

target_link_libraries(test_network
  zenremote_network
  gtest
  gtest_main
)

add_test(NAME NetworkTests COMMAND test_network)
```

---

## 七、实现路线 (Timeline)

### Sprint 2, Week 3-5: 网络传输层

**Week 3: DirectConnection 和 RTP 基础**
- [ ] `direct_connection.h/cpp` - UDP Socket 封装
- [ ] `rtp_packet.h` - RTP Header 定义和序列化
- [ ] 握手协议实现
- [ ] 单元测试 (连接建立、包格式)

**Week 4: Sender 和 Receiver**
- [ ] `rtp_sender.h/cpp` - 打包和发送
- [ ] `rtp_receiver.h/cpp` - 解析和接收
- [ ] 丢包检测
- [ ] 基本错误处理

**Week 5: Pacer 和 JitterBuffer**
- [ ] `pacer.h/cpp` - 固定间隔发送 (5ms)
- [ ] `jitter_buffer.h/cpp` - 固定缓冲 (50ms)
- [ ] 集成到 ControllerSession/ControlledSession
- [ ] 端到端延迟测试

**里程碑**: Windows → UDP → Mac, 视频可解码渲染

---

### Sprint 6-8: Phase 2 扩展 (公网支持)

**前置**: Phase 1 稳定运行

**Week 1-2: 信令服务器和连接管理**
- [ ] `connection_manager.h/cpp` - 候选地址收集和连接选择
- [ ] `base_connection.h` - 抽象基类
- [ ] WebSocket 客户端集成
- [ ] 候选地址收集 (local, public IP)

**Week 3: TURN 集成**
- [ ] `turn_connection.h/cpp` - TURN 协议实现
- [ ] coturn 部署
- [ ] 自动切换 (direct → turn)
- [ ] TURN 测试

**Week 4: 拥塞控制和 NACK**
- [ ] `congestion_controller.h/cpp` - GCC 算法
- [ ] `nack_handler.h/cpp` - 选择性重传
- [ ] 自适应 JitterBuffer
- [ ] 公网环境下性能测试

**里程碑**: 任意网络环境都能连接

---

## 八、测试策略

### 8.1 单元测试

**RTP 包格式**:
```cpp
TEST(RTPPacket, Serialize) {
  RTPHeader hdr;
  hdr.payload_type = 96;
  hdr.sequence_number = 100;
  hdr.timestamp = 1000;
  hdr.ssrc = 0x12345678;
  hdr.marker = true;

  uint8_t buffer[12];
  ASSERT_TRUE(hdr.Serialize(buffer, 12));

  RTPHeader hdr2;
  ASSERT_TRUE(hdr2.Deserialize(buffer, 12));
  ASSERT_EQ(hdr2.sequence_number, 100);
  ASSERT_EQ(hdr2.timestamp, 1000);
}
```

**Pacer 逻辑**:
```cpp
TEST(Pacer, FixedInterval) {
  Pacer pacer({5});  // 5ms 间隔
  
  ASSERT_TRUE(pacer.CanSend());
  pacer.OnPacketSent();
  
  // 立即再试,应该失败
  ASSERT_FALSE(pacer.CanSend());
  
  // 等待 5ms
  std::this_thread::sleep_for(std::chrono::milliseconds(6));
  ASSERT_TRUE(pacer.CanSend());
}
```

**JitterBuffer 乱序处理**:
```cpp
TEST(JitterBuffer, ReorderPackets) {
  JitterBuffer buf({50, 100});
  
  // 插入乱序包
  ReceivedPacket pkt1, pkt2;
  pkt1.header.sequence_number = 2;
  pkt1.header.timestamp = 1000;
  pkt2.header.sequence_number = 1;
  pkt2.header.timestamp = 1000;
  
  buf.InsertPacket(pkt1);  // 先插入 seq=2
  buf.InsertPacket(pkt2);  // 再插入 seq=1
  
  std::vector<uint8_t> frame;
  uint32_t ts;
  ASSERT_TRUE(buf.TryExtractFrame(frame, ts));
  ASSERT_EQ(ts, 1000);
}
```

### 8.2 集成测试

**握手和连接**:
1. 启动 DirectConnection (被控端,监听)
2. 启动 DirectConnection (控制端,连接)
3. 验证握手成功
4. 发送/接收测试包

**视频传输**:
1. 打开两个 Session
2. 控制端发送视频帧(RTP 打包)
3. 被控端接收并缓冲
4. 验证帧完整性和顺序

### 8.3 性能测试

**指标**:
- 连接建立时间: < 100ms
- RTP 打包开销: < 1%
- 平均延迟: < 5ms (LAN)
- 抖动: < 2ms
- 内存占用: < 10MB

**工具**:
```bash
# 测试 UDP 吞吐量
iperf3 -u -b 10M -t 60

# 测试网络延迟
ping -c 100 remote_ip | tail -5

# 监控内存
top -p <pid>
```

---

## 九、已知限制与 TODO

### Phase 1 限制

| 限制项 | 说明 | Phase 2 方案 |
|-------|------|-----------|
| **固定 IP** | 需手动配置 | 信令服务器自动交换 |
| **同网络** | 无 NAT 穿透 | TURN 中继 |
| **无加密** | 明文传输 | DTLS 加密(可选) |
| **无拥塞控制** | 固定码率 | GCC 算法 |
| **无重传** | 丢包即丢 | NACK 重传 |
| **固定缓冲** | 50ms 死板 | 自适应算法 |

### 扩展点

- ✅ **WebRTC 互操作**: 使用标准 RTP/RTCP,便于未来与 WebRTC 融合
- ✅ **DTLS 加密** (Phase 2.5)
- ✅ **Simulcast** (多码率自适应)
- ✅ **FEC** (前向纠错)
- ✅ **SVC** (可伸缩编码)

---

## 十、参考代码框架

### 10.1 DirectConnection 初始实现

```cpp
// src/network/direct_connection.h
#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace zenremote {

class DirectConnection {
 public:
  struct Config {
    std::string local_ip;
    uint16_t local_port;
    std::string remote_ip;
    uint16_t remote_port;
    int socket_buffer_size = 1024 * 1024;
    int recv_timeout_ms = 1000;
  };

  struct Stats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_received = 0;
    uint32_t rtt_ms = 0;
  };

  explicit DirectConnection(const Config& config);
  ~DirectConnection();

  bool Open();
  void Close();
  bool IsOpen() const { return socket_fd_ >= 0; }

  bool SendPacket(const uint8_t* data, size_t length);
  bool RecvPacket(uint8_t* buffer, size_t& length, 
                  int timeout_ms = 1000);

  const Stats& GetStats() const { return stats_; }

 private:
  Config config_;
  int socket_fd_ = -1;
  Stats stats_;

  bool CreateSocket();
  bool BindSocket();
  bool SetSocketOptions();
};

}  // namespace zenremote
```

### 10.2 RTP 包定义

```cpp
// src/network/rtp_packet.h
#pragma once

#include <cstdint>
#include <vector>

namespace zenremote {

struct RTPHeader {
  static constexpr size_t kHeaderSize = 12;  // bytes

  uint8_t version = 2;
  bool padding = false;
  bool extension = false;
  uint8_t csrc_count = 0;
  bool marker = false;
  uint8_t payload_type = 0;  // 96=Video, 97=Audio, 98=Control
  uint16_t sequence_number = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;

  bool Serialize(uint8_t* buffer, size_t length);
  bool Deserialize(const uint8_t* buffer, size_t length);
};

struct RTPPacket {
  RTPHeader header;
  std::vector<uint8_t> payload;
};

}  // namespace zenremote
```

### 10.3 输入事件定义

```cpp
// src/network/input_event.h
#pragma once

#include <cstdint>

namespace zenremote {

enum class InputEventType : uint8_t {
  kMouseMove = 0,
  kMouseClick = 1,
  kMouseWheel = 2,
  kKeyDown = 3,
  kKeyUp = 4,
  kTouchEvent = 5,
};

struct InputEvent {
  InputEventType type;
  uint16_t x;           // Mouse X coordinate
  uint16_t y;           // Mouse Y coordinate
  uint8_t button;       // Mouse button: 1=left, 2=right, 3=middle
  uint8_t state;        // 0=up, 1=down
  int16_t wheel_delta;  // Scroll value
  uint32_t key_code;    // Virtual key code
  uint32_t modifier_keys;  // Ctrl/Shift/Alt/Cmd flags

  // Serialization for RTP payload
  std::vector<uint8_t> ToBytes() const;
  static InputEvent FromBytes(const uint8_t* data, size_t length);
};

// Control message for reliable delivery
struct ControlMessageHeader {
  uint16_t sequence_number;
  uint32_t timestamp;
  bool requires_ack;
  uint8_t reserved;
};

struct AckMessage {
  uint16_t acked_sequence_number;
  uint32_t timestamp;  // Original message timestamp for RTT calculation
};

}  // namespace zenremote
```

### 10.4 输入事件可靠发送

```cpp
// src/network/reliable_input_sender.h
#pragma once

#include "input_event.h"
#include "rtp_packet.h"
#include <queue>
#include <memory>
#include <chrono>

namespace zenremote {

class DirectConnection;

class ReliableInputSender {
 public:
  explicit ReliableInputSender(uint32_t ssrc, DirectConnection* connection);

  // Send mouse/keyboard event with reliable delivery
  bool SendInputEvent(const InputEvent& event);

  // Handle incoming ACK
  void OnAckMessage(const AckMessage& ack);

  // Retry pending messages periodically
  void ProcessRetries();

  // Get sender statistics
  struct Stats {
    uint64_t events_sent = 0;
    uint64_t events_acked = 0;
    uint64_t events_retried = 0;
    uint64_t events_failed = 0;
  };
  const Stats& GetStats() const { return stats_; }

 private:
  struct PendingMessage {
    InputEvent event;
    uint16_t sequence_number;
    std::chrono::steady_clock::time_point send_time;
    int retry_count = 0;
    
    static constexpr int kMaxRetries = 3;
    static constexpr int kRetryTimeoutMs = 50;
  };

  uint32_t ssrc_;
  DirectConnection* connection_;
  uint16_t next_sequence_number_ = 0;
  std::queue<PendingMessage> pending_messages_;
  Stats stats_;

  bool SendViaRTP(const InputEvent& event, uint16_t seq);
};

}  // namespace zenremote
```

## 附录 C: 控制消息协议选择对比

### 为什么选择 UDP + 应用层可靠性而非 TCP/DataChannel?

**场景分析**:
```
需求:
- 鼠标/键盘事件需要可靠送达
- 但不能因为控制消息延迟而卡住视频/音频
- 输入响应延迟需要 < 50ms
- 局域网环境丢包率极低 (< 0.1%)
```

| 对比维度 | UDP + App层重试 | TCP | WebRTC DataChannel |
|---------|----------------|-----|------------------|
| **可靠性** | 99.9% (3次重试) | 100% | 100% |
| **延迟** | 0-100ms (正常 < 50ms) | 10-200ms (slow start) | 50-500ms (WebRTC库开销) |
| **建立时间** | 0ms (无连接) | 100-300ms (3-way) | 1-5s (ICE + DTLS) |
| **局域网适配** | 完美 | 过度设计 | 严重过度设计 |
| **单 Socket 支持** | ✅ 原生 | ❌ 需单独连接 | ❌ 独立 DataChannel |
| **与媒体流集成** | ✅ 简单 | ❌ 复杂 | ❌ 极复杂 |
| **阻塞行为** | 非阻塞(丢包不卡) | **阻塞**(重传等待) | 非阻塞 |
| **CPU 开销** | < 0.1% | 1-2% | 2-5% |
| **代码复杂度** | 简单 (~500 lines) | 中等 | 极复杂 (WebRTC库) |
| **调试友好** | ✅ Wireshark 可见所有包 | ✅ 同 | ❌ 加密,黑盒 |
| **跨平台开发** | ✅ 标准 Socket | ✅ 标准 | ❌ WebRTC 库差异 |
| **Phase 2 升级** | ✅ 平滑升级 (添加 TURN/GCC) | ✗ 需要重构 | ✗ 可能锁定 |

**关键决策理由**:

1. **不用 TCP 的原因**:
   ```
   鼠标事件丢包 → TCP 重传 → 延迟增加 → 控制卡顿
   
   示例: 100 个鼠标事件/秒
   - 丢包 1% 时,TCP 平均延迟增加 50ms (1次重传)
   - 但视频仍需 30-50ms Jitter Buffer
   - 总延迟可能超过 200ms (无法接受)
   
   UDP + 3 次重试:
   - 大多数时间 0ms 延迟
   - 丢包重试时 50-100ms (极端情况)
   - 但不阻塞其他流
   ```

2. **不用 DataChannel 的原因**:
   ```
   DataChannel 是 WebRTC 概念,需要:
   - 完整的 WebRTC 库 (1000+ 源文件)
   - SCTP 协议栈 (额外复杂度)
   - DTLS 加密 (局域网无需)
   - ICE 候选地址收集 (Phase 1 手动 IP)
   
   对于 Phase 1:
   - 增加 200+ MB 编译体积
   - 3-5s 会话建立延迟
   - 无法利用之前的 UDP Socket
   
   Phase 2 if needed:
   - 可选添加 WebRTC 库
   - 但 Phase 1 不依赖它
   ```

3. **为什么选择 UDP**:
   ```
   ✅ 完全控制延迟 (无 TCP slow start)
   ✅ 与媒体流共享 Socket (降低复杂度)
   ✅ 应用层可靠性足以覆盖局域网 99.7% 场景
   ✅ 便于诊断 (所有包在 Wireshark 中可见)
   ✅ Phase 2 可平滑升级 (仅添加拥塞控制,不改底层协议)
   ```

**验证对比**:

**场景 1: 局域网直连 (正常条件)**
```
TCP 表现:
- 连接建立: 100-200ms
- 送达延迟: 平均 10ms (TCP_NODELAY 下)
- 吞吐量: 相同
- 问题: 初始建立慢

UDP + App层:
- 连接建立: 0ms (无连接)
- 送达延迟: 平均 1ms
- 吞吐量: 相同
- 优势: ✅ 10 倍快

DataChannel:
- 连接建立: 2-5s
- 送达延迟: 平均 50ms
- 吞吐量: 相同
- 问题: ❌ 完全不适合
```

**场景 2: 网络抖动 (1% 丢包)**
```
TCP 表现:
- 丢包重传: 引起 50-100ms 延迟峰值
- 问题: 输入卡顿,用户感受差

UDP + App层:
- 大多数丢包: 立即重试 (50ms 内重发)
- 媒体流: 不受影响 (视频继续播放)
- 优势: ✅ 输入不卡顿

DataChannel:
- SCTP 层处理: 100-200ms 重试
- 问题: ❌ 比 TCP 更慢
```

**验证公式**:
```
TCP 平均延迟 (有丢包):
  D_tcp = D_base + (loss_rate × RTT_retransmit)
  
  例: D_base=10ms, loss=1%, RTT_retrans=50ms
  D_tcp = 10 + 0.01 × 50 = 10.5ms
  ✓ 看起来不错,但丢包时的峰值延迟可能达到 50ms+

UDP + 3 重试:
  成功率 = 1 - (loss_rate^3) = 1 - 0.01^3 = 99.9%
  D_avg = D_base × 0.999 + (D_base + 50ms) × 0.001
  D_avg ≈ 10.05ms (极端情况)
  ✅ 保证 99.9% 成功率,同时延迟极低
```

### 总结

**Phase 1 采用 UDP + 应用层可靠性是最佳平衡点**:
- ✅ 极低延迟 (0-5ms 通常情况)
- ✅ 足够可靠 (99.9% 成功率)
- ✅ 最小代码复杂度 (~500 lines 可实现)
- ✅ 便于未来升级 (Phase 2 仅添加 TURN/GCC,无需改协议)
- ✅ 跨平台标准化 (标准 Socket,无依赖)

**Phase 2 如需增强**:
- 添加 GCC 拥塞控制 (动态码率调整)
- 添加 NACK 重传 (关键帧优先)
- 可选 DTLS 加密 (如需安全传输)
- **但保留 UDP 底层** (不改为 TCP/DataChannel)

---

```bash
# Windows: 查看网络统计
netstat -an | find "50000"

# macOS/Linux: 查看 UDP 套接字
lsof -i :50000

# 测试 UDP 连通性
nc -u -l 50000  # 被控端
nc -u 192.168.1.100 50000  # 控制端发送测试
```

---

## 附录 B: 故障排查表

| 问题 | 可能原因 | 解决方案 |
|------|---------|--------|
| 连接失败 | 防火墙阻止 | 开放 UDP 端口 |
| 无法接收 | 绑定地址错误 | 检查 `local_ip` 配置 |
| 丢包严重 | 网络带宽不足 | 降低码率或帧率 |
| 延迟高 | 缓冲设置过大 | 调整 `jitter_buffer_ms` |
| 声音不同步 | 时间戳计算错误 | 检查 RTP 时间戳生成逻辑 |

---

**设计完成。** 该文档提供了 Phase 1 (局域网直连) 和 Phase 2 (公网扩展) 的完整 UDP 传输层设计,确保 Phase 1 代码无需改动即可升级到 Phase 2。

