#ifdef _WIN32

#include "d3d11_renderer.h"

#include <d3dcompiler.h>

#include "../codec/decoder/hw_decoder_context.h"
#include "common/log_manager.h"
#include "common/timer_util.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace zenremote {

// ======================== HLSL 着色器代码 ========================

static const char* kVertexShaderSource = R"(
struct VS_INPUT {
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.tex = input.tex;
    return output;
}
)";

static const char* kPixelShaderNV12Source = R"(
Texture2D<float> texY : register(t0);
Texture2D<float2> texUV : register(t1);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float y = texY.Sample(samplerState, input.tex);
    float2 uv = texUV.Sample(samplerState, input.tex);
    
    // YUV to RGB (BT.709)
    float r = y + 1.5748f * (uv.x - 0.5f);
    float g = y - 0.1873f * (uv.y - 0.5f) - 0.4681f * (uv.x - 0.5f);
    float b = y + 1.8556f * (uv.y - 0.5f);
    
    return float4(saturate(r), saturate(g), saturate(b), 1.0f);
}
)";

// 顶点结构
struct Vertex {
  float x, y;  // 位置
  float u, v;  // 纹理坐标
};

// 全屏四边形顶�?
static const Vertex kFullscreenQuad[] = {
    {-1.0f, -1.0f, 0.0f, 1.0f},  // 左下
    {-1.0f, 1.0f, 0.0f, 0.0f},   // 左上
    {1.0f, -1.0f, 1.0f, 1.0f},   // 右下
    {1.0f, 1.0f, 1.0f, 0.0f},    // 右上
};

// ======================== D3D11Renderer 实现 ========================

D3D11Renderer::D3D11Renderer() {
  QueryPerformanceFrequency(&perf_freq_);
  QueryPerformanceCounter(&last_fps_time_);
}

D3D11Renderer::~D3D11Renderer() {
  Shutdown();
}

Result<void> D3D11Renderer::Initialize(const RendererConfig& config) {
  if (initialized_) {
    return Result<void>::Err(ErrorCode::kAlreadyInitialized,
                             "D3D11Renderer already initialized");
  }

  if (!config.window_handle) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Window handle is required");
  }

  HWND hwnd = static_cast<HWND>(config.window_handle);

  video_width_ = config.width;
  video_height_ = config.height;
  hw_context_ = config.hw_context;

  // 如果提供了硬件解码上下文，尝试使用其 D3D11 设备
  if (hw_context_ && hw_context_->GetD3D11Device()) {
    device_.Attach(hw_context_->GetD3D11Device());
    device_->AddRef();

    context_.Attach(hw_context_->GetD3D11DeviceContext());
    context_->AddRef();

    zero_copy_enabled_ = true;
    ZENREMOTE_INFO(
        "D3D11Renderer using shared device from HW decoder (zero-copy "
        "enabled)");
  }

  // 创建设备和交换链
  auto device_result = CreateDeviceAndSwapChain(hwnd);
  if (device_result.IsErr()) {
    return device_result;
  }

  // 创建渲染目标视图
  auto rtv_result = CreateRenderTargetView();
  if (rtv_result.IsErr()) {
    return rtv_result;
  }

  // 创建着色器
  auto shader_result = CreateShaders();
  if (shader_result.IsErr()) {
    return shader_result;
  }

  // 创建采样器状�?
  auto sampler_result = CreateSamplerState();
  if (sampler_result.IsErr()) {
    return sampler_result;
  }

  // 创建视频纹理
  auto texture_result =
      CreateVideoTexture(video_width_, video_height_, DXGI_FORMAT_NV12);
  if (texture_result.IsErr()) {
    return texture_result;
  }

  // 获取窗口尺寸
  RECT rect;
  GetClientRect(hwnd, &rect);
  window_width_ = rect.right - rect.left;
  window_height_ = rect.bottom - rect.top;

  initialized_ = true;
  stats_ = RenderStats{};

  ZENREMOTE_INFO("D3D11Renderer initialized: {}x{}, zero-copy: {}",
                 video_width_, video_height_, zero_copy_enabled_);

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::CreateDeviceAndSwapChain(HWND hwnd) {
  // 如果设备已经从硬件解码器获取，只创建交换�?
  if (!device_) {
    UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL achieved_level;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   create_flags, feature_levels,
                                   ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
                                   &device_, &achieved_level, &context_);

    if (FAILED(hr)) {
      return Result<void>::Err(ErrorCode::kRenderError,
                               fmt::format("D3D11CreateDevice failed: 0x{:08X}",
                                           static_cast<unsigned>(hr)));
    }

    ZENREMOTE_DEBUG("D3D11 device created, feature level: 0x{:X}",
                    static_cast<unsigned>(achieved_level));
  }

  // 获取 DXGI Factory
  Microsoft::WRL::ComPtr<IDXGIDevice2> dxgi_device;
  HRESULT hr = device_.As(&dxgi_device);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to get DXGI device");
  }

  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  hr = dxgi_device->GetAdapter(&adapter);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to get DXGI adapter");
  }

  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  hr = adapter->GetParent(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to get DXGI factory");
  }

  // 创建交换�?
  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
  swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = 2;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  hr = factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &swap_chain_desc,
                                       nullptr, nullptr, &swap_chain_);

  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("CreateSwapChainForHwnd failed: 0x{:08X}",
                    static_cast<unsigned>(hr)));
  }

  // 禁用 Alt+Enter 全屏切换
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::CreateRenderTargetView() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to get swap chain back buffer");
  }

  hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                       &render_target_view_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create render target view");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::CreateShaders() {
  Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
  Microsoft::WRL::ComPtr<ID3DBlob> ps_blob;
  Microsoft::WRL::ComPtr<ID3DBlob> error_blob;

  // 编译顶点着色器
  HRESULT hr =
      D3DCompile(kVertexShaderSource, strlen(kVertexShaderSource),
                 "VertexShader", nullptr, nullptr, "main", "vs_4_0",
                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_blob, &error_blob);

  if (FAILED(hr)) {
    std::string error_msg = "Unknown error";
    if (error_blob) {
      error_msg = static_cast<const char*>(error_blob->GetBufferPointer());
    }
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Vertex shader compile failed: {}", error_msg));
  }

  hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                   vs_blob->GetBufferSize(), nullptr,
                                   &vertex_shader_);

  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create vertex shader");
  }

  // 编译像素着色器
  hr = D3DCompile(kPixelShaderNV12Source, strlen(kPixelShaderNV12Source),
                  "PixelShader", nullptr, nullptr, "main", "ps_4_0",
                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_blob, &error_blob);

  if (FAILED(hr)) {
    std::string error_msg = "Unknown error";
    if (error_blob) {
      error_msg = static_cast<const char*>(error_blob->GetBufferPointer());
    }
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Pixel shader compile failed: {}", error_msg));
  }

  hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                  ps_blob->GetBufferSize(), nullptr,
                                  &pixel_shader_);

  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create pixel shader");
  }

  // 创建输入布局
  D3D11_INPUT_ELEMENT_DESC input_desc[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  hr = device_->CreateInputLayout(input_desc, ARRAYSIZE(input_desc),
                                  vs_blob->GetBufferPointer(),
                                  vs_blob->GetBufferSize(), &input_layout_);

  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create input layout");
  }

  // 创建顶点缓冲
  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = sizeof(kFullscreenQuad);
  buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA init_data = {};
  init_data.pSysMem = kFullscreenQuad;

  hr = device_->CreateBuffer(&buffer_desc, &init_data, &vertex_buffer_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create vertex buffer");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::CreateSamplerState() {
  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  HRESULT hr = device_->CreateSamplerState(&sampler_desc, &sampler_state_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create sampler state");
  }

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::CreateVideoTexture(int width,
                                               int height,
                                               DXGI_FORMAT format) {
  // 清理旧纹�?
  video_texture_srv_y_.Reset();
  video_texture_srv_uv_.Reset();
  video_texture_.Reset();

  D3D11_TEXTURE2D_DESC tex_desc = {};
  tex_desc.Width = width;
  tex_desc.Height = height;
  tex_desc.MipLevels = 1;
  tex_desc.ArraySize = 1;
  tex_desc.Format = format;
  tex_desc.SampleDesc.Count = 1;
  tex_desc.Usage = D3D11_USAGE_DEFAULT;
  tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  HRESULT hr = device_->CreateTexture2D(&tex_desc, nullptr, &video_texture_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create video texture");
  }

  // 创建 Y 平面 SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc_y = {};
  srv_desc_y.Format = DXGI_FORMAT_R8_UNORM;
  srv_desc_y.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc_y.Texture2D.MipLevels = 1;

  hr = device_->CreateShaderResourceView(video_texture_.Get(), &srv_desc_y,
                                         &video_texture_srv_y_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create Y plane SRV");
  }

  // 创建 UV 平面 SRV
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc_uv = {};
  srv_desc_uv.Format = DXGI_FORMAT_R8G8_UNORM;
  srv_desc_uv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc_uv.Texture2D.MipLevels = 1;

  hr = device_->CreateShaderResourceView(video_texture_.Get(), &srv_desc_uv,
                                         &video_texture_srv_uv_);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to create UV plane SRV");
  }

  video_width_ = width;
  video_height_ = height;

  return Result<void>::Ok();
}

void D3D11Renderer::Shutdown() {
  if (!initialized_) {
    return;
  }

  // 清理 D3D11 资源
  video_texture_srv_y_.Reset();
  video_texture_srv_uv_.Reset();
  video_texture_.Reset();
  sampler_state_.Reset();
  vertex_buffer_.Reset();
  input_layout_.Reset();
  pixel_shader_.Reset();
  vertex_shader_.Reset();
  render_target_view_.Reset();
  swap_chain_.Reset();

  // 如果使用共享设备，不释放（由硬件解码上下文管理）
  if (!zero_copy_enabled_) {
    context_.Reset();
    device_.Reset();
  } else {
    context_.Detach();
    device_.Detach();
  }

  hw_context_ = nullptr;
  initialized_ = false;

  ZENREMOTE_INFO("D3D11Renderer shutdown, rendered {} frames",
                 stats_.frames_rendered);
}

Result<void> D3D11Renderer::Render(const AVFrame* frame) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "D3D11Renderer not initialized");
  }

  if (!frame) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Null frame pointer");
  }

  TIMER_START(render);

  Result<void> result;

  // 根据帧类型选择渲染路径
  if (frame->format == AV_PIX_FMT_D3D11 && zero_copy_enabled_) {
    result = RenderHWFrame(frame);
  } else {
    result = RenderSWFrame(frame);
  }

  if (result.IsErr()) {
    return result;
  }

  // 呈现
  HRESULT hr = swap_chain_->Present(1, 0);  // 1 = VSync
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::kRenderError,
        fmt::format("Present failed: 0x{:08X}", static_cast<unsigned>(hr)));
  }

  double render_time = TIMER_END_MS(render);

  UpdateStats(render_time);

  return Result<void>::Ok();
}

Result<void> D3D11Renderer::RenderHWFrame(const AVFrame* frame) {
  // �?AVFrame 获取 D3D11 纹理
  ID3D11Texture2D* hw_texture =
      reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
  int texture_index = reinterpret_cast<intptr_t>(frame->data[1]);

  if (!hw_texture) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Invalid hardware texture");
  }

  // 复制到渲染纹�?
  D3D11_BOX src_box = {};
  src_box.right = frame->width;
  src_box.bottom = frame->height;
  src_box.back = 1;

  context_->CopySubresourceRegion(video_texture_.Get(), 0, 0, 0, 0, hw_texture,
                                  texture_index, &src_box);

  // 设置渲染状态并绘制
  return RenderSWFrame(nullptr);  // 使用已复制的纹理
}

Result<void> D3D11Renderer::RenderSWFrame(const AVFrame* frame) {
  // 如果有帧，上传到纹理
  if (frame) {
    // 检查尺寸是否改�?
    if (frame->width != video_width_ || frame->height != video_height_) {
      auto result =
          CreateVideoTexture(frame->width, frame->height, DXGI_FORMAT_NV12);
      if (result.IsErr()) {
        return result;
      }
    }

    // 上传数据
    context_->UpdateSubresource(video_texture_.Get(), 0, nullptr,
                                frame->data[0], frame->linesize[0], 0);
  }

  // 设置渲染管线
  context_->OMSetRenderTargets(1, render_target_view_.GetAddressOf(), nullptr);

  // 设置视口
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(window_width_);
  viewport.Height = static_cast<float>(window_height_);
  viewport.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &viewport);

  // 清除
  float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
  context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);

  // 设置着色器
  context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
  context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
  context_->IASetInputLayout(input_layout_.Get());

  // 设置顶点缓冲
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  context_->IASetVertexBuffers(0, 1, vertex_buffer_.GetAddressOf(), &stride,
                               &offset);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  // 设置纹理和采样器
  ID3D11ShaderResourceView* srvs[] = {video_texture_srv_y_.Get(),
                                      video_texture_srv_uv_.Get()};
  context_->PSSetShaderResources(0, 2, srvs);
  context_->PSSetSamplers(0, 1, sampler_state_.GetAddressOf());

  // 绘制
  context_->Draw(4, 0);

  return Result<void>::Ok();
}

void D3D11Renderer::Clear() {
  if (!initialized_) {
    return;
  }

  float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
  context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);
  swap_chain_->Present(0, 0);
}

Result<void> D3D11Renderer::OnResize(int width, int height) {
  if (!initialized_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "D3D11Renderer not initialized");
  }

  if (width <= 0 || height <= 0) {
    return Result<void>::Ok();
  }

  // 释放旧的渲染目标
  render_target_view_.Reset();

  // 调整交换�?
  HRESULT hr =
      swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    return Result<void>::Err(ErrorCode::kRenderError,
                             "Failed to resize swap chain");
  }

  // 重新创建渲染目标
  auto rtv_result = CreateRenderTargetView();
  if (rtv_result.IsErr()) {
    return rtv_result;
  }

  window_width_ = width;
  window_height_ = height;

  ZENREMOTE_DEBUG("D3D11Renderer resized to {}x{}", width, height);
  return Result<void>::Ok();
}

void D3D11Renderer::UpdateStats(double render_time_ms) {
  stats_.frames_rendered++;
  frames_since_last_update_++;

  total_render_time_ms_ += render_time_ms;
  stats_.avg_render_time_ms = total_render_time_ms_ / stats_.frames_rendered;

  // 每秒更新 FPS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double elapsed = static_cast<double>(now.QuadPart - last_fps_time_.QuadPart) /
                   perf_freq_.QuadPart;

  if (elapsed >= 1.0) {
    stats_.fps = frames_since_last_update_ / elapsed;
    last_fps_time_ = now;
    frames_since_last_update_ = 0;
  }
}

RenderStats D3D11Renderer::GetStats() const {
  return stats_;
}

}  // namespace zenremote

#endif  // _WIN32
