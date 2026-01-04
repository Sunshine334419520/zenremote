#pragma once

#include "video_renderer.h"

#ifdef _WIN32

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace zenremote {

// 前向声明
class HWDecoderContext;

/// @brief Direct3D 11 视频渲染器
///
/// 基于 Direct3D 11 的高性能渲染器，支持与硬件解码器的零拷贝渲染。
/// 优势：性能优秀、支持零拷贝、与 D3D11VA 无缝集成
/// 劣势：仅限 Windows 平台
class D3D11Renderer : public IVideoRenderer {
 public:
  D3D11Renderer();
  ~D3D11Renderer() override;

  // 禁止拷贝
  D3D11Renderer(const D3D11Renderer&) = delete;
  D3D11Renderer& operator=(const D3D11Renderer&) = delete;

  // IVideoRenderer 接口实现
  Result<void> Initialize(const RendererConfig& config) override;
  void Shutdown() override;
  Result<void> Render(const AVFrame* frame) override;
  void Clear() override;
  Result<void> OnResize(int width, int height) override;
  bool IsInitialized() const override { return initialized_; }
  RendererType GetType() const override { return RendererType::kD3D11; }
  RenderStats GetStats() const override;
  std::string GetName() const override { return "D3D11 Renderer"; }
  bool SupportsZeroCopy() const override { return zero_copy_enabled_; }

 private:
  /// @brief 创建 D3D11 设备和交换链
  Result<void> CreateDeviceAndSwapChain(HWND hwnd);

  /// @brief 创建渲染目标视图
  Result<void> CreateRenderTargetView();

  /// @brief 创建着色器资源
  Result<void> CreateShaders();

  /// @brief 创建视频纹理
  Result<void> CreateVideoTexture(int width, int height, DXGI_FORMAT format);

  /// @brief 创建采样器状态
  Result<void> CreateSamplerState();

  /// @brief 编译着色器
  Result<void> CompileShaders();

  /// @brief 从硬件解码帧渲染（零拷贝）
  Result<void> RenderHWFrame(const AVFrame* frame);

  /// @brief 从软件帧渲染（需要上传）
  Result<void> RenderSWFrame(const AVFrame* frame);

  /// @brief 更新统计信息
  void UpdateStats(double render_time_ms);

  // D3D11 核心对象
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;

  // 视频纹理
  Microsoft::WRL::ComPtr<ID3D11Texture2D> video_texture_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> video_texture_srv_y_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> video_texture_srv_uv_;

  // 着色器
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;

  // 顶点缓冲和采样器
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_state_;

  // 硬件解码上下文（用于零拷贝）
  HWDecoderContext* hw_context_ = nullptr;

  bool initialized_ = false;
  bool zero_copy_enabled_ = false;

  int video_width_ = 0;
  int video_height_ = 0;
  int window_width_ = 0;
  int window_height_ = 0;

  // 统计信息
  mutable RenderStats stats_;
  double total_render_time_ms_ = 0;
  LARGE_INTEGER perf_freq_;
  LARGE_INTEGER last_fps_time_;
  uint64_t frames_since_last_update_ = 0;
};

}  // namespace zenremote

#endif  // _WIN32
