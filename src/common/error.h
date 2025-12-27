#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace zenremote {

/**
 * @brief 统一的错误码枚举
 *
 * 设计原则：
 * - 按功能模块分类，便于快速定位问题
 * - 每个模块预留足够的码位供后续扩展
 * - 0 表示成功，便于 C 风格 API 兼容
 *
 * 码位规划：
 *   0: 成功
 *   1-99: 通用错误
 *   100-199: 网络连接层 (Connection/Socket)
 *   200-299: 网络协议层 (RTP/Reliable/Handshake)
 *   300-399: 传输层 (MediaTrack/DataChannel/PeerConnection)
 *   400-499: 媒体采集层 (ScreenCapturer/AudioCapture)
 *   500-599: 编解码层 (Encoder/Decoder)
 *   600-699: 音频处理层 (AudioDevice/Resampler)
 *   700-799: 系统资源层 (Memory/Thread/System)
 *   800-899: 配置层 (Config/Parameter)
 */
enum class ErrorCode : int {
  // ============ 成功与通用错误 (0-99) ============
  kSuccess = 0,             ///< 操作成功
  kInvalidParameter = 1,    ///< 参数无效
  kNotInitialized = 2,      ///< 模块未初始化
  kAlreadyInitialized = 3,  ///< 模块已初始化
  kAlreadyRunning = 4,      ///< 已在运行
  kNotRunning = 5,          ///< 未在运行
  kInvalidState = 6,        ///< 无效状态转换
  kInvalidOperation = 7,    ///< 无效操作
  kNotImplemented = 8,      ///< 功能未实现
  kNotSupported = 9,        ///< 不支持的功能
  kUnknown = 99,            ///< 未知错误

  // ============ 网络连接层 (100-199) ============
  kNetworkError = 100,         ///< 网络通用错误
  kConnectionTimeout = 101,    ///< 连接超时
  kConnectionRefused = 102,    ///< 连接被拒绝
  kConnectionFailed = 103,     ///< 连接失败
  kNetworkUnreachable = 104,   ///< 网络不可达
  kSocketError = 105,          ///< Socket 错误
  kSocketBindFailed = 106,     ///< Socket 绑定失败
  kSocketListenFailed = 107,   ///< Socket 监听失败
  kSocketConnectFailed = 108,  ///< Socket 连接失败
  kSocketSendFailed = 109,     ///< Socket 发送失败
  kSocketRecvFailed = 110,     ///< Socket 接收失败
  kDNSLookupFailed = 111,      ///< DNS 查询失败
  kInvalidAddress = 112,       ///< 无效的地址
  kPortUnavailable = 113,      ///< 端口不可用

  // ============ 网络协议层 (200-299) ============
  kProtocolError = 200,           ///< 协议错误
  kRTPError = 201,                ///< RTP 错误
  kRTPHeaderInvalid = 202,        ///< RTP 头无效
  kRTPPayloadInvalid = 203,       ///< RTP 负载无效
  kRTPSequenceError = 204,        ///< RTP 序列号错误
  kHandshakeFailed = 205,         ///< 握手失败
  kHandshakeTimeout = 206,        ///< 握手超时
  kReliableTransportError = 207,  ///< 可靠传输错误
  kPacketLoss = 208,              ///< 包丢失
  kPacketOutOfOrder = 209,        ///< 包乱序
  kBufferOverflow = 210,          ///< 缓冲区溢出
  kBufferUnderflow = 211,         ///< 缓冲区不足
  kJitterBufferError = 212,       ///< 抖动缓冲区错误

  // ============ 传输层 (300-399) ============
  kTransportError = 300,       ///< 传输层通用错误
  kMediaTrackError = 301,      ///< 媒体轨道错误
  kAudioTrackError = 302,      ///< 音频轨道错误
  kVideoTrackError = 303,      ///< 视频轨道错误
  kDataChannelError = 304,     ///< 数据通道错误
  kPeerConnectionError = 305,  ///< 对等连接错误
  kTrackDisabled = 306,        ///< 轨道已禁用
  kTrackNotConnected = 307,    ///< 轨道未连接
  kChannelClosed = 308,        ///< 通道已关闭
  kChannelFull = 309,          ///< 通道已满

  // ============ 媒体采集层 (400-499) ============
  kCaptureError = 400,              ///< 采集通用错误
  kScreenCapturerError = 401,       ///< 屏幕采集错误
  kScreenCapturerInitFailed = 402,  ///< 屏幕采集初始化失败
  kDXGIError = 403,                 ///< DXGI 错误
  kDesktopDuplicationError = 404,   ///< 桌面复制错误
  kAudioCaptureError = 405,         ///< 音频采集错误
  kCaptureFormatInvalid = 406,      ///< 采集格式无效
  kCaptureResolutionInvalid = 407,  ///< 采集分辨率无效
  kCaptureTimeoutError = 408,       ///< 采集超时

  // ============ 编解码层 (500-599) ============
  kCodecError = 500,              ///< 编解码通用错误
  kEncoderError = 501,            ///< 编码器错误
  kEncoderNotFound = 502,         ///< 未找到编码器
  kEncoderInitFailed = 503,       ///< 编码器初始化失败
  kEncodeFailed = 504,            ///< 编码失败
  kDecoderError = 505,            ///< 解码器错误
  kDecoderNotFound = 506,         ///< 未找到解码器
  kDecoderInitFailed = 507,       ///< 解码器初始化失败
  kDecodeFailed = 508,            ///< 解码失败
  kUnsupportedCodec = 509,        ///< 不支持的编码格式
  kUnsupportedPixelFormat = 510,  ///< 不支持的像素格式
  kInvalidBitrate = 511,          ///< 无效的比特率
  kInvalidFrameRate = 512,        ///< 无效的帧率

  // ============ 音频处理层 (600-699) ============
  kAudioError = 600,                     ///< 音频通用错误
  kAudioDeviceError = 601,               ///< 音频设备错误
  kAudioDeviceNotFound = 602,            ///< 音频设备未找到
  kAudioDeviceNotInitialized = 603,      ///< 音频设备未初始化
  kAudioDeviceAlreadyInitialized = 604,  ///< 音频设备已初始化
  kAudioOutputError = 605,               ///< 音频输出错误
  kAudioFormatNotSupported = 606,        ///< 不支持的音频格式
  kAudioResampleError = 607,             ///< 重采样错误
  kAudioBufferError = 608,               ///< 音频缓冲区错误

  // ============ 系统资源层 (700-799) ============
  kSystemError = 700,         ///< 系统通用错误
  kOutOfMemory = 701,         ///< 内存不足
  kThreadError = 702,         ///< 线程错误
  kThreadCreateFailed = 703,  ///< 线程创建失败
  kTimeout = 704,             ///< 操作超时
  kInternalError = 705,       ///< 内部错误
  kResourceExhausted = 706,   ///< 资源耗尽
  kPermissionDenied = 707,    ///< 权限被拒绝
  kIOError = 708,             ///< IO 错误
  kFileNotFound = 709,        ///< 文件不存在
  kFileAccessDenied = 710,    ///< 文件访问被拒绝

  // ============ 配置层 (800-899) ============
  kConfigError = 800,            ///< 配置错误
  kConfigInvalid = 801,          ///< 配置无效
  kConfigNotFound = 802,         ///< 配置未找到
  kConfigVersionMismatch = 803,  ///< 配置版本不匹配

  // ============ FFmpeg 相关 (900-999) ============
  kEndOfFile = 900,        ///< 文件结束
  kInvalidFormat = 901,    ///< 无效格式
  kDemuxerNotFound = 902,  ///< 解混器未找到
  kStreamNotFound = 903,   ///< 流未找到
  kNetworkTimeout = 904,   ///< 网络超时
  kBufferTooSmall = 905,   ///< 缓冲区太小
  kRenderError = 906,      ///< 渲染错误
};

/**
 * @brief 获取 ErrorCode 的可读字符串表示
 */
inline const char* ErrorCodeToString(ErrorCode code) {
  switch (code) {
    // 成功与通用
    case ErrorCode::kSuccess:
      return "Success";
    case ErrorCode::kInvalidParameter:
      return "InvalidParameter";
    case ErrorCode::kNotInitialized:
      return "NotInitialized";
    case ErrorCode::kAlreadyInitialized:
      return "AlreadyInitialized";
    case ErrorCode::kAlreadyRunning:
      return "AlreadyRunning";
    case ErrorCode::kNotRunning:
      return "NotRunning";
    case ErrorCode::kInvalidState:
      return "InvalidState";
    case ErrorCode::kInvalidOperation:
      return "InvalidOperation";
    case ErrorCode::kNotImplemented:
      return "NotImplemented";
    case ErrorCode::kNotSupported:
      return "NotSupported";
    case ErrorCode::kUnknown:
      return "Unknown";

    // 网络连接层
    case ErrorCode::kNetworkError:
      return "NetworkError";
    case ErrorCode::kConnectionTimeout:
      return "ConnectionTimeout";
    case ErrorCode::kConnectionRefused:
      return "ConnectionRefused";
    case ErrorCode::kConnectionFailed:
      return "ConnectionFailed";
    case ErrorCode::kNetworkUnreachable:
      return "NetworkUnreachable";
    case ErrorCode::kSocketError:
      return "SocketError";
    case ErrorCode::kSocketBindFailed:
      return "SocketBindFailed";
    case ErrorCode::kSocketListenFailed:
      return "SocketListenFailed";
    case ErrorCode::kSocketConnectFailed:
      return "SocketConnectFailed";
    case ErrorCode::kSocketSendFailed:
      return "SocketSendFailed";
    case ErrorCode::kSocketRecvFailed:
      return "SocketRecvFailed";
    case ErrorCode::kDNSLookupFailed:
      return "DNSLookupFailed";
    case ErrorCode::kInvalidAddress:
      return "InvalidAddress";
    case ErrorCode::kPortUnavailable:
      return "PortUnavailable";

    // 网络协议层
    case ErrorCode::kProtocolError:
      return "ProtocolError";
    case ErrorCode::kRTPError:
      return "RTPError";
    case ErrorCode::kRTPHeaderInvalid:
      return "RTPHeaderInvalid";
    case ErrorCode::kRTPPayloadInvalid:
      return "RTPPayloadInvalid";
    case ErrorCode::kRTPSequenceError:
      return "RTPSequenceError";
    case ErrorCode::kHandshakeFailed:
      return "HandshakeFailed";
    case ErrorCode::kHandshakeTimeout:
      return "HandshakeTimeout";
    case ErrorCode::kReliableTransportError:
      return "ReliableTransportError";
    case ErrorCode::kPacketLoss:
      return "PacketLoss";
    case ErrorCode::kPacketOutOfOrder:
      return "PacketOutOfOrder";
    case ErrorCode::kBufferOverflow:
      return "BufferOverflow";
    case ErrorCode::kBufferUnderflow:
      return "BufferUnderflow";
    case ErrorCode::kJitterBufferError:
      return "JitterBufferError";

    // 传输层
    case ErrorCode::kTransportError:
      return "TransportError";
    case ErrorCode::kMediaTrackError:
      return "MediaTrackError";
    case ErrorCode::kAudioTrackError:
      return "AudioTrackError";
    case ErrorCode::kVideoTrackError:
      return "VideoTrackError";
    case ErrorCode::kDataChannelError:
      return "DataChannelError";
    case ErrorCode::kPeerConnectionError:
      return "PeerConnectionError";
    case ErrorCode::kTrackDisabled:
      return "TrackDisabled";
    case ErrorCode::kTrackNotConnected:
      return "TrackNotConnected";
    case ErrorCode::kChannelClosed:
      return "ChannelClosed";
    case ErrorCode::kChannelFull:
      return "ChannelFull";

    // 媒体采集层
    case ErrorCode::kCaptureError:
      return "CaptureError";
    case ErrorCode::kScreenCapturerError:
      return "ScreenCapturerError";
    case ErrorCode::kScreenCapturerInitFailed:
      return "ScreenCapturerInitFailed";
    case ErrorCode::kDXGIError:
      return "DXGIError";
    case ErrorCode::kDesktopDuplicationError:
      return "DesktopDuplicationError";
    case ErrorCode::kAudioCaptureError:
      return "AudioCaptureError";
    case ErrorCode::kCaptureFormatInvalid:
      return "CaptureFormatInvalid";
    case ErrorCode::kCaptureResolutionInvalid:
      return "CaptureResolutionInvalid";
    case ErrorCode::kCaptureTimeoutError:
      return "CaptureTimeoutError";

    // 编解码层
    case ErrorCode::kCodecError:
      return "CodecError";
    case ErrorCode::kEncoderError:
      return "EncoderError";
    case ErrorCode::kEncoderNotFound:
      return "EncoderNotFound";
    case ErrorCode::kEncoderInitFailed:
      return "EncoderInitFailed";
    case ErrorCode::kEncodeFailed:
      return "EncodeFailed";
    case ErrorCode::kDecoderError:
      return "DecoderError";
    case ErrorCode::kDecoderNotFound:
      return "DecoderNotFound";
    case ErrorCode::kDecoderInitFailed:
      return "DecoderInitFailed";
    case ErrorCode::kDecodeFailed:
      return "DecodeFailed";
    case ErrorCode::kUnsupportedCodec:
      return "UnsupportedCodec";
    case ErrorCode::kUnsupportedPixelFormat:
      return "UnsupportedPixelFormat";
    case ErrorCode::kInvalidBitrate:
      return "InvalidBitrate";
    case ErrorCode::kInvalidFrameRate:
      return "InvalidFrameRate";

    // 音频处理层
    case ErrorCode::kAudioError:
      return "AudioError";
    case ErrorCode::kAudioDeviceError:
      return "AudioDeviceError";
    case ErrorCode::kAudioDeviceNotFound:
      return "AudioDeviceNotFound";
    case ErrorCode::kAudioDeviceNotInitialized:
      return "AudioDeviceNotInitialized";
    case ErrorCode::kAudioDeviceAlreadyInitialized:
      return "AudioDeviceAlreadyInitialized";
    case ErrorCode::kAudioOutputError:
      return "AudioOutputError";
    case ErrorCode::kAudioFormatNotSupported:
      return "AudioFormatNotSupported";
    case ErrorCode::kAudioResampleError:
      return "AudioResampleError";
    case ErrorCode::kAudioBufferError:
      return "AudioBufferError";

    // 系统资源层
    case ErrorCode::kSystemError:
      return "SystemError";
    case ErrorCode::kOutOfMemory:
      return "OutOfMemory";
    case ErrorCode::kThreadError:
      return "ThreadError";
    case ErrorCode::kThreadCreateFailed:
      return "ThreadCreateFailed";
    case ErrorCode::kTimeout:
      return "Timeout";
    case ErrorCode::kInternalError:
      return "InternalError";
    case ErrorCode::kResourceExhausted:
      return "ResourceExhausted";
    case ErrorCode::kPermissionDenied:
      return "PermissionDenied";
    case ErrorCode::kIOError:
      return "IOError";
    case ErrorCode::kFileNotFound:
      return "FileNotFound";
    case ErrorCode::kFileAccessDenied:
      return "FileAccessDenied";

    // 配置层
    case ErrorCode::kConfigError:
      return "ConfigError";
    case ErrorCode::kConfigInvalid:
      return "ConfigInvalid";
    case ErrorCode::kConfigNotFound:
      return "ConfigNotFound";
    case ErrorCode::kConfigVersionMismatch:
      return "ConfigVersionMismatch";

    // FFmpeg 相关
    case ErrorCode::kEndOfFile:
      return "EndOfFile";
    case ErrorCode::kInvalidFormat:
      return "InvalidFormat";
    case ErrorCode::kDemuxerNotFound:
      return "DemuxerNotFound";
    case ErrorCode::kStreamNotFound:
      return "StreamNotFound";
    case ErrorCode::kNetworkTimeout:
      return "NetworkTimeout";
    case ErrorCode::kBufferTooSmall:
      return "BufferTooSmall";
    case ErrorCode::kRenderError:
      return "RenderError";

    default:
      return "UnknownErrorCode";
  }
}

/**
 * @brief 统一的结果类型模板
 *
 * 使用示例：
 *   // 返回成功的值
 *   Result<int> r = Result<int>::Ok(42);
 *   if (r.IsOk()) { std::cout << r.Value(); }
 *
 *   // 返回错误
 *   Result<int> err = Result<int>::Err(ErrorCode::kInvalidParameter, "param
 * must > 0"); if (!err.IsOk()) { std::cout << "Error: " << err.Message();
 *   }
 *
 * 设计要点：
 *   1. 轻量级：仅包含必要的字段（value, error_code, message）
 *   2. 零开销：模板实例化，内联友好
 *   3. 移动语义：支持高效的所有权转移
 *   4. 便捷：提供便利方法（IsOk, IsErr, Err等）
 */
template <typename T>
class Result {
 public:
  using ValueType = T;

  /**
   * @brief 构造成功结果
   */
  static Result Ok(T value) {
    return Result(std::move(value), ErrorCode::kSuccess, std::string());
  }

  /**
   * @brief 构造失败结果
   */
  static Result Err(ErrorCode code, std::string message = std::string()) {
    return Result(T(), code, std::move(message));
  }

  /**
   * @brief 默认构造函数（创建未初始化的结果）
   * 通常不建议使用，仅为兼容性提供
   */
  Result() : error_code_(ErrorCode::kNotInitialized), message_() {}

  /**
   * @brief 移动构造函数
   */
  Result(Result&& other) noexcept
      : value_(std::move(other.value_)),
        error_code_(other.error_code_),
        message_(std::move(other.message_)) {}

  /**
   * @brief 移动赋值操作符
   */
  Result& operator=(Result&& other) noexcept {
    if (this != &other) {
      value_ = std::move(other.value_);
      error_code_ = other.error_code_;
      message_ = std::move(other.message_);
    }
    return *this;
  }

  /**
   * @brief 禁用拷贝构造和拷贝赋值（避免意外深拷贝）
   */
  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  ~Result() = default;

  // ============ 查询方法 ============

  /**
   * @brief 检查结果是否成功
   */
  bool IsOk() const { return error_code_ == ErrorCode::kSuccess; }

  /**
   * @brief 检查结果是否失败
   */
  bool IsErr() const { return error_code_ != ErrorCode::kSuccess; }

  /**
   * @brief 获取错误码
   */
  ErrorCode Code() const { return error_code_; }

  /**
   * @brief 获取错误消息
   */
  const std::string& Message() const { return message_; }

  /**
   * @brief 获取错误码的字符串表示
   */
  const char* CodeString() const { return ErrorCodeToString(error_code_); }

  // ============ 值访问方法 ============

  /**
   * @brief 获取值的可变引用
   * 注意：仅当 IsOk() 为 true 时才应调用此方法
   */
  T& Value() { return value_; }

  /**
   * @brief 获取值的不可变引用
   */
  const T& Value() const { return value_; }

  /**
   * @brief 获取值的所有权（移动）
   * 例如：std::unique_ptr 所有权转移
   * 注意：调用后对象中的值处于被移出状态
   */
  T TakeValue() { return std::move(value_); }

  /**
   * @brief 操作符重载：->
   * 允许直接访问值的成员（如果 T 是指针或有 operator*）
   */
  T* operator->() { return &value_; }

  const T* operator->() const { return &value_; }

  /**
   * @brief 操作符重载：*
   * 允许解引用（如果 T 是指针类型）
   */
  T& operator*() { return value_; }

  const T& operator*() const { return value_; }

  // ============ 链式操作 ============

  /**
   * @brief 如果当前是 Ok，应用函数返回新的 Result
   * 用于链式调用，便于函数组合
   *
   * 示例：
   *   Result<int> r = ParseInt("42")
   *     .AndThen([](int v) { return Result<int>::Ok(v * 2); });
   */
  template <typename F>
  auto AndThen(F&& f) -> std::invoke_result_t<F, T> {
    using ResultType = std::invoke_result_t<F, T>;
    if (!IsOk()) {
      return ResultType::Err(error_code_, message_);
    }
    return std::forward<F>(f)(std::move(value_));
  }

  /**
   * @brief 如果当前是 Err，应用错误处理函数返回新值
   * 用于错误恢复
   *
   * 示例：
   *   Result<int> r = OpenFile("missing.txt")
   *     .OrElse([](ErrorCode e) { return Result<int>::Ok(0); });
   */
  template <typename F>
  Result<T> OrElse(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return std::forward<F>(f)(error_code_);
  }

  /**
   * @brief 如果当前是 Ok，应用映射函数到值，返回新的 Result<U>
   *
   * 示例：
   *   Result<std::string> r = Result<int>::Ok(42)
   *     .Map([](int v) { return std::to_string(v); });
   */
  template <typename F>
  Result<std::invoke_result_t<F, T>> Map(F&& f) {
    using ReturnType = std::invoke_result_t<F, T>;
    if (!IsOk()) {
      return Result<ReturnType>::Err(error_code_, message_);
    }
    return Result<ReturnType>::Ok(std::forward<F>(f)(std::move(value_)));
  }

  /**
   * @brief 如果当前是 Err，应用映射函数到错误码
   *
   * 示例：
   *   Result<int> r = SomeOperation()
   *     .MapErr([](ErrorCode e) { return ErrorCode::kUnknown; });
   */
  template <typename F>
  Result<T> MapErr(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return Result<T>::Err(std::forward<F>(f)(error_code_), message_);
  }

  // ============ 便捷方法 ============

  /**
   * @brief 获取值，或在失败时返回默认值
   *
   * 示例：
   *   int value = result.ValueOr(0);
   */
  T ValueOr(T default_value) const {
    return IsOk() ? value_ : std::move(default_value);
  }

  /**
   * @brief 获取完整的错误消息字符串（包含错误码）
   *
   * 返回格式：[ErrorCodeString] message
   */
  std::string FullMessage() const {
    std::string full_msg = CodeString();
    if (!message_.empty()) {
      full_msg += ": ";
      full_msg += message_;
    }
    return full_msg;
  }

 private:
  // 私有构造函数
  Result(T value, ErrorCode code, std::string message)
      : value_(std::move(value)),
        error_code_(code),
        message_(std::move(message)) {}

  T value_;
  ErrorCode error_code_;
  std::string message_;
};

/**
 * @brief Result<void> 特化版本
 *
 * 当不需要返回值时使用，仅用于传递成功/失败状态和错误消息。
 * 使用示例：
 *   Result<void> Init() {
 *     if (!ValidateConfig()) {
 *       return Result<void>::Err(ErrorCode::kInvalidParameter, "config
 * invalid");
 *     }
 *     return Result<void>::Ok();
 *   }
 */
template <>
class Result<void> {
 public:
  /**
   * @brief 构造成功结果
   */
  static Result<void> Ok() {
    return Result<void>(ErrorCode::kSuccess, std::string());
  }

  /**
   * @brief 构造失败结果
   */
  static Result<void> Err(ErrorCode code, std::string message = std::string()) {
    return Result<void>(code, std::move(message));
  }

  /**
   * @brief 默认构造函数
   */
  Result<void>() : error_code_(ErrorCode::kNotInitialized), message_() {}

  /**
   * @brief 移动构造函数
   */
  Result<void>(Result&& other) noexcept
      : error_code_(other.error_code_), message_(std::move(other.message_)) {}

  /**
   * @brief 移动赋值操作符
   */
  Result<void>& operator=(Result<void>&& other) noexcept {
    if (this != &other) {
      error_code_ = other.error_code_;
      message_ = std::move(other.message_);
    }
    return *this;
  }

  Result<void>(const Result<void>&) = delete;
  Result<void>& operator=(const Result<void>&) = delete;

  ~Result<void>() = default;

  // ============ 查询方法 ============

  bool IsOk() const { return error_code_ == ErrorCode::kSuccess; }

  bool IsErr() const { return error_code_ != ErrorCode::kSuccess; }

  // 显式转换为 bool，用于测试断言 EXPECT_TRUE(result)
  explicit operator bool() const { return IsOk(); }

  ErrorCode Code() const { return error_code_; }

  const std::string& Message() const { return message_; }

  const char* CodeString() const { return ErrorCodeToString(error_code_); }

  // ============ 链式操作 ============

  /**
   * @brief 如果成功，执行给定的函数，并链式返回
   */
  template <typename F>
  Result<void> AndThen(F&& f) {
    if (!IsOk()) {
      return std::move(*this);
    }
    return std::forward<F>(f)();
  }

  /**
   * @brief 如果失败，执行错误处理并返回 Ok
   */
  template <typename F>
  Result<void> OrElse(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    std::forward<F>(f)(error_code_);
    return Result<void>::Ok();
  }

  /**
   * @brief 如果当前是 Err，应用映射函数到错误码
   *
   * 示例：
   *   Result<void> r = SomeOperation()
   *     .MapErr([](ErrorCode e) { return ErrorCode::kUnknown; });
   */
  template <typename F>
  Result<void> MapErr(F&& f) {
    if (IsOk()) {
      return std::move(*this);
    }
    return Result<void>::Err(std::forward<F>(f)(error_code_), message_);
  }

  /**
   * @brief 获取完整的错误消息字符串
   */
  std::string FullMessage() const {
    std::string full_msg = CodeString();
    if (!message_.empty()) {
      full_msg += ": ";
      full_msg += message_;
    }
    return full_msg;
  }

 private:
  explicit Result<void>(ErrorCode code, std::string message)
      : error_code_(code), message_(std::move(message)) {}

  ErrorCode error_code_;
  std::string message_;
};

/**
 * @brief 便捷类型别名
 */
using VoidResult = Result<void>;

/**
 * @brief 输出流操作符（便于调试和日志记录）
 */
template <typename T>
inline std::ostream& operator<<(std::ostream& os, const Result<T>& result) {
  os << result.FullMessage();
  return os;
}

}  // namespace zenremote
