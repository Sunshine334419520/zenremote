#pragma once

#include <cstddef>
#include <cstdint>

#include "common/error.h"

namespace zenremote {

/**
 * @brief 连接类型
 */
enum class ConnectionType {
  kDirect,  ///< 直连 (Phase 1: 局域网 UDP 直连)
  kRelay,   ///< 中继 (Phase 2: TURN 服务器中继)
};

/**
 * @brief 传输层抽象接口 - 所有连接实现的基类
 *
 * 职责：
 * - 定义统一的连接接口
 * - 提供 Open/Close/Send/Recv 抽象方法
 * - 支持多态：DirectConnection 和 TurnConnection 都实现此接口
 *
 * 设计理念：
 * - ConnectionManager 依赖此接口，不依赖具体实现
 * - Phase 2 添加 TurnConnection 无需修改 ConnectionManager
 * - 协议层 (RTPSender/Receiver) 通过此接口发送数据
 *
 * 实现类：
 * - DirectConnection: 局域网直连 (Phase 1)
 * - TurnConnection: TURN 中继 (Phase 2)
 */
class BaseConnection {
 public:
  virtual ~BaseConnection() = default;

  /**
   * @brief 打开连接
   * @return 成功返回 Ok，失败返回错误信息
   */
  virtual Result<void> Open() = 0;

  /**
   * @brief 关闭连接
   */
  virtual void Close() = 0;

  /**
   * @brief 检查连接是否已打开
   */
  virtual bool IsOpen() const = 0;

  /**
   * @brief 发送数据
   * @param data 数据指针
   * @param length 数据长度
   * @return 成功返回发送的字节数，失败返回错误信息
   */
  virtual Result<size_t> Send(const uint8_t* data, size_t length) = 0;

  /**
   * @brief 接收数据
   * @param buffer 接收缓冲区
   * @param buffer_size 缓冲区大小
   * @param timeout_ms 超时时间 (毫秒)
   * @return 成功返回接收的字节数，超时或失败返回错误信息
   */
  virtual Result<size_t> Recv(uint8_t* buffer,
                              size_t buffer_size,
                              int timeout_ms) = 0;

  /**
   * @brief 获取连接类型
   */
  virtual ConnectionType GetType() const = 0;
};

}  // namespace zenremote
