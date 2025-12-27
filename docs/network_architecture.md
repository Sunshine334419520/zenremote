# 网络传输层架构说明

## 分层设计

```
┌──────────────────────────────────────────────────┐
│  应用层 (Application Layer)                      │
│  - ControllerSession / ControlledSession         │
│  ↓ 只依赖 ConnectionManager                      │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────┴────────────────────────────────┐
│  连接管理层 (Connection Manager Layer)          │
│  - ConnectionManager (统一连接接口)            │
│  - 自动选择 Direct/TURN                         │
│  ↓ 内部使用 BaseConnection                      │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────┴────────────────────────────────┐
│  传输层 (Transport Layer)                       │
│  - BaseConnection (抽象接口)                   │
│  - DirectConnection (Phase 1: 局域网直连)      │
│  - TurnConnection (Phase 2: TURN 中继)         │
│  ↓ 使用 UdpSocket                               │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────┴────────────────────────────────┐
│  网络 I/O 层 (Network I/O Layer)                │
│  - UdpSocket (纯 UDP Socket 封装)              │
│  - 跨平台支持 (Windows/Linux/macOS)            │
└──────────────────────────────────────────────────┘

协议层 (Protocol Layer) - 横向支持
├── RTPSender / RTPReceiver
├── HandshakeManager
├── Pacer / JitterBuffer
└── ReliableInputSender/Receiver
```

## 关键优势

### 1. 代码复用
- `UdpSocket` 只实现一次，被所有传输层复用
- `DirectConnection` 和 `TurnConnection` 都使用同一个 `UdpSocket`
- 避免重复实现 socket 操作、错误处理、平台差异

### 2. 清晰的职责分离

#### UdpSocket (网络 I/O 层)
```cpp
class UdpSocket {
  bool Open();                    // 创建和绑定 socket
  void Close();                   // 关闭 socket
  bool SendTo(ip, port, data);    // 发送到指定地址
  bool RecvFrom(buffer, &ip, &port); // 从任意地址接收
  bool WaitForRead(timeout);      // 等待可读
};
```

**职责**：
- 纯粹的 UDP Socket 操作
- 跨平台兼容 (Winsock2 / BSD sockets)
- 错误处理和日志记录
- 统计信息收集

**不负责**：
- 不知道远程端点是谁
- 不知道连接状态
- 不处理协议逻辑

#### DirectConnection (传输层)
```cpp
class DirectConnection : public BaseConnection {
  Result<void> Initialize(config);  // 初始化连接
  Result<size_t> Send(data);        // 发送到固定远程端点
  Result<size_t> Recv(buffer);      // 接收数据
  
 private:
  std::unique_ptr<UdpSocket> socket_;  // 使用 UdpSocket
  Endpoint remote_endpoint_;           // 管理远程端点
};
```

**职责**：
- 管理固定的远程端点 (IP + Port)
- 实现 BaseConnection 接口
- 调用 UdpSocket 进行实际 I/O

**不负责**：
- 不直接操作 socket
- 不处理平台差异

#### TurnConnection (Phase 2 传输层)
```cpp
class TurnConnection : public BaseConnection {
  Result<void> AllocateRelay();     // TURN 协议: 分配中继地址
  Result<void> SendTurnPacket();    // 通过 TURN 发送
  
 private:
  std::unique_ptr<UdpSocket> socket_;  // 复用同一个 UdpSocket!
  std::string relay_address_;          // TURN 中继地址
};
```

**职责**：
- 实现 TURN 协议逻辑
- 管理 TURN 中继地址
- 仍然使用 `UdpSocket` 进行底层通信

**优势**：
- 无需重新实现 UDP Socket 封装
- 与 `DirectConnection` 共享同一套 I/O 代码

### 3. 可测试性

```cpp
// 测试 UdpSocket (独立测试网络 I/O)
TEST(UdpSocket, SendRecv) {
  UdpSocket sender({local_ip: "127.0.0.1", local_port: 50000});
  UdpSocket receiver({local_ip: "127.0.0.1", local_port: 50001});
  
  sender.SendTo(data, "127.0.0.1", 50001);
  receiver.RecvFrom(buffer, from_ip, from_port);
}

// 测试 DirectConnection (测试传输层逻辑)
TEST(DirectConnection, Initialize) {
  DirectConnection conn;
  conn.Initialize({remote: {"192.168.1.100", 50000}});
  conn.Send(data);
}
```

### 4. 扩展性

#### Phase 1: 局域网直连
```cpp
auto connection = std::make_unique<DirectConnection>();
connection->Initialize({
  remote: {"192.168.1.100", 50000}
});
```

#### Phase 2: TURN 中继
```cpp
auto connection = std::make_unique<TurnConnection>();
connection->Initialize({
  turn_server_ip: "turn.example.com",
  turn_server_port: 3478,
  username: "user",
  password: "pass"
});
```

#### 应用层无需修改
```cpp
// RTPSender 只依赖 BaseConnection 接口
class RTPSender {
  RTPSender(BaseConnection* connection);  // 多态
  bool SendVideoFrame(frame);
};

// 可以用 DirectConnection 或 TurnConnection
RTPSender sender(connection.get());
```

## 代码对比

### 之前的设计问题 ❌
```cpp
class DirectConnection {
 private:
  SOCKET socket_;
  
  bool CreateSocket() { socket_ = socket(...); }
  bool SendTo(...) { sendto(socket_, ...); }
  bool RecvFrom(...) { recvfrom(socket_, ...); }
};

class TurnConnection {
 private:
  SOCKET socket_;  // 重复实现!
  
  bool CreateSocket() { socket_ = socket(...); }  // 重复代码!
  bool SendTo(...) { sendto(socket_, ...); }      // 重复代码!
  bool RecvFrom(...) { recvfrom(socket_, ...); }  // 重复代码!
};
```

**问题**：
- Socket 操作重复实现
- 跨平台代码重复
- 错误处理逻辑重复
- 难以维护和测试

### 改进后的设计 ✅
```cpp
// 网络 I/O 层 (只实现一次)
class UdpSocket {
  bool SendTo(ip, port, data);
  bool RecvFrom(buffer, &ip, &port);
};

// 传输层 (复用 UdpSocket)
class DirectConnection : public BaseConnection {
 private:
  std::unique_ptr<UdpSocket> socket_;  // 复用!
  Endpoint remote_endpoint_;
};

class TurnConnection : public BaseConnection {
 private:
  std::unique_ptr<UdpSocket> socket_;  // 复用同一套代码!
  std::string relay_address_;
};
```

**优势**：
- ✅ Socket 操作只实现一次
- ✅ 代码复用率高
- ✅ 易于维护和测试
- ✅ 清晰的职责分离
- ✅ Phase 2 扩展简单

## 文件组织

```
src/network/
├── io/                          # 网络 I/O 层 (Layer 1)
│   ├── udp_socket.h
│   └── udp_socket.cpp
│
├── transport/                   # 传输层 (Layer 2)
│   ├── base_connection.h       # 连接抽象接口
│   ├── direct_connection.h     # Phase 1: 直连实现
│   ├── direct_connection.cpp
│   └── turn_connection.h       # Phase 2: TURN 实现 (待实现)
│
├── connection_manager/          # 连接管理层 (Layer 3)
│   ├── connection_manager.h    # 统一连接管理接口
│   └── connection_manager.cpp
│
└── protocol/                    # 协议层 (Layer 4)
    ├── packet.h                # RTP 包定义
    ├── protocol.h              # 控制消息定义
    ├── rtp_sender.h/cpp        # RTP 发送器
    ├── rtp_receiver.h/cpp      # RTP 接收器
    ├── handshake.h/cpp         # 握手协议
    ├── pacer.h/cpp             # 流量调度
    ├── jitter_buffer.h/cpp     # 抖动缓冲
    └── reliable_input.h/cpp    # 可靠输入传输
```

## 关键设计亮点

### 1. 连接管理层的必要性

**为什么需要 ConnectionManager？**

```cpp
// ❌ 错误做法：应用层直接依赖具体连接
class ControllerSession {
  DirectConnection connection_;  // Phase 2 要改成 TurnConnection 怎么办？
};

// ✅ 正确做法：应用层只依赖 ConnectionManager
class ControllerSession {
  ConnectionManager conn_mgr_;   // Phase 2 无需修改应用层代码！
};
```

**优势**：
- 应用层代码完全隔离底层连接实现
- Phase 2 添加 TURN，应用层零改动
- 可以在运行时动态切换连接方式
- 统一的错误处理和统计接口

### 2. 应用层使用示例

```cpp
// Phase 1 使用示例
class ControllerSession {
 public:
  Result<void> Initialize(const std::string& remote_ip, uint16_t remote_port) {
    // 配置连接管理器
    ConnectionManager::Config config;
    config.mode = ConnectionManager::Mode::kDirect;  // Phase 1: 直连
    config.remote_ip = remote_ip;
    config.remote_port = remote_port;
    
    // 初始化并连接
    auto result = conn_mgr_.Initialize(config);
    if (result.IsErr()) return result;
    
    return conn_mgr_.Connect();
  }
  
  void SendVideoFrame(const VideoFrame& frame) {
    // 应用层无需关心底层是 Direct 还是 TURN
    conn_mgr_.Send(frame.data, frame.size);
  }
  
 private:
  ConnectionManager conn_mgr_;  // 唯一依赖！
};

// Phase 2 使用示例 - 应用层代码完全相同！
class ControllerSession {
 public:
  Result<void> Initialize(const TurnConfig& turn_config) {
    ConnectionManager::Config config;
    config.mode = ConnectionManager::Mode::kAuto;  // 自动选择
    config.turn_server_ip = turn_config.server_ip;
    config.turn_server_port = turn_config.server_port;
    
    // 其他代码完全相同！
    auto result = conn_mgr_.Initialize(config);
    if (result.IsErr()) return result;
    
    return conn_mgr_.Connect();
  }
  
  // SendVideoFrame() 完全不用改！
};
```

## 总结

通过引入 **连接管理层 (ConnectionManager)**，我们实现了：

1. **单一职责原则**：每层只做自己的事
   - Layer 1: 纯 Socket 操作
   - Layer 2: 连接实现 (Direct/TURN)
   - Layer 3: 连接管理和选择
   - Layer 4: 协议处理

2. **代码复用**：所有传输层共享同一套 Socket 代码

3. **开闭原则**：扩展 Phase 2 无需修改 Phase 1
   - 应用层代码零改动
   - 只需在 Layer 2 添加 TurnConnection
   - 只需在 Layer 3 实现自动选择逻辑

4. **依赖倒置**：
   - 应用层依赖 ConnectionManager (抽象)
   - ConnectionManager 依赖 BaseConnection (抽象)
   - BaseConnection 依赖 UdpSocket (实现)

5. **应用透明**：应用层永远只与 ConnectionManager 交互，底层切换完全透明

这是正确的、可扩展的分层架构！🎯
