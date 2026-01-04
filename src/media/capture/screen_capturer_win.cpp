#include "screen_capturer_win.h"

#include <chrono>
#include <thread>

#include "common/log_manager.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

namespace zenremote::media::capture {

namespace {

constexpr UINT CAPTURE_TIMEOUT_MS = 1000;  // 1 秒超时: 防止死锁

std::string HrToString(HRESULT hr) {
  switch (hr) {
    case DXGI_ERROR_WAIT_TIMEOUT:
      return "DXGI_ERROR_WAIT_TIMEOUT (No new frame)";
    case DXGI_ERROR_ACCESS_LOST:
      return "DXGI_ERROR_ACCESS_LOST (Output lost)";
    case DXGI_ERROR_DEVICE_RESET:
      return "DXGI_ERROR_DEVICE_RESET (GPU reset)";
    case DXGI_ERROR_DEVICE_REMOVED:
      return "DXGI_ERROR_DEVICE_REMOVED (GPU removed)";
    case E_INVALIDARG:
      return "E_INVALIDARG (Invalid argument)";
    case E_ACCESSDENIED:
      return "E_ACCESSDENIED (Access denied)";
    default:
      return "0x" + std::to_string(static_cast<uint32_t>(hr));
  }
}

}  // namespace

ScreenCapturerDxgi::ScreenCapturerDxgi()
    : d3d_device_(nullptr),
      d3d_context_(nullptr),
      dxgi_output_dup_(nullptr),
      staging_texture_(nullptr),
      is_initialized_(false),
      is_running_(false),
      width_(0),
      height_(0),
      stride_(0),
      current_fps_(0),
      frame_count_(0),
      should_force_key_frame_(false) {}

ScreenCapturerDxgi::~ScreenCapturerDxgi() {
  Shutdown();
}

bool ScreenCapturerDxgi::Initialize(const CaptureConfig& config) {
  if (is_initialized_) {
    ZENREMOTE_WARN("ScreenCapturerDxgi already initialized");
    return true;
  }

  config_ = config;
  ZENREMOTE_INFO("Initializing DXGI screen capturer for output {}",
                 config.output_index);

  // Step 1: 创建 DXGI Factory
  IDXGIFactory1* factory = nullptr;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    ZENREMOTE_ERROR("CreateDXGIFactory1 failed: {}", HrToString(hr));
    return false;
  }

  // Step 2: 枚举 GPU 适配器: 选择最优的(通常是独立显卡)
  IDXGIAdapter* adapter = SelectBestAdapter(factory);
  if (!adapter) {
    ZENREMOTE_ERROR("No suitable GPU adapter found");
    factory->Release();
    return false;
  }

  // Step 3: 创建 Direct3D 11 设备
  D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
  hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                         feature_levels, 1, D3D11_SDK_VERSION, &d3d_device_,
                         nullptr, &d3d_context_);

  adapter->Release();
  factory->Release();

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("D3D11CreateDevice failed: {}", HrToString(hr));
    return false;
  }

  ZENREMOTE_INFO("D3D11 device created successfully");

  // Step 4: 获取 DXGI 输出(显示器)
  if (!GetDxgiOutput(config.output_index)) {
    d3d_context_->Release();
    d3d_device_->Release();
    return false;
  }

  // Step 5: 创建 Desktop Duplication
  if (!CreateDesktopDuplication()) {
    output_->Release();
    d3d_context_->Release();
    d3d_device_->Release();
    return false;
  }

  // Step 6: 获取屏幕分辨率和创建 Staging Texture
  if (!SetupFrameBuffer()) {
    dxgi_output_dup_->Release();
    output_->Release();
    d3d_context_->Release();
    d3d_device_->Release();
    return false;
  }

  is_initialized_ = true;
  ZENREMOTE_INFO("ScreenCapturerDxgi initialized: {}x{}", width_, height_);
  return true;
}

bool ScreenCapturerDxgi::Start() {
  if (!is_initialized_) {
    ZENREMOTE_ERROR("ScreenCapturerDxgi not initialized");
    return false;
  }

  if (is_running_) {
    ZENREMOTE_WARN("ScreenCapturerDxgi already running");
    return true;
  }

  is_running_ = true;
  frame_count_ = 0;
  fps_timer_.Reset();

  ZENREMOTE_INFO("ScreenCapturerDxgi started");
  return true;
}

void ScreenCapturerDxgi::Stop() {
  is_running_ = false;
  ZENREMOTE_INFO("ScreenCapturerDxgi stopped");
}

std::optional<Frame> ScreenCapturerDxgi::CaptureFrame() {
  if (!is_running_) {
    return std::nullopt;
  }

  DXGI_OUTDUPL_FRAME_INFO frame_info{};
  IDXGIResource* frame_resource = nullptr;

  // 获取下一帧: 使用固定 1 秒超时
  HRESULT hr =
      dxgi_output_dup_->AcquireNextFrame(1000, &frame_info, &frame_resource);

  // 处理各种返回状态
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
    // 正常情况:没有新帧,等待超时
    return std::nullopt;
  }

  if (hr == DXGI_ERROR_ACCESS_LOST) {
    ZENREMOTE_WARN("Output access lost, recreating duplication");
    RecreateOutputDuplication();
    return std::nullopt;
  }

  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    ZENREMOTE_ERROR("GPU error: {}", HrToString(hr));
    // 后续会通过定期重初始化恢复
    return std::nullopt;
  }

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("AcquireNextFrame failed: {}", HrToString(hr));
    return std::nullopt;
  }

  // 成功获得帧
  // 注意: frame_resource 的生命周期只到 ReleaseFrame(),必须立即复制

  ID3D11Texture2D* frame_texture = nullptr;
  hr = frame_resource->QueryInterface(IID_PPV_ARGS(&frame_texture));
  frame_resource->Release();

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("QueryInterface ID3D11Texture2D failed: {}",
                    HrToString(hr));
    dxgi_output_dup_->ReleaseFrame();
    return std::nullopt;
  }

  // 复制到 Staging Texture(CPU 可访问)
  d3d_context_->CopyResource(staging_texture_, frame_texture);
  frame_texture->Release();

  // 映射 Staging Texture 以获取像素数据
  D3D11_MAPPED_SUBRESOURCE mapped_resource{};
  hr = d3d_context_->Map(staging_texture_, 0, D3D11_MAP_READ, 0,
                         &mapped_resource);

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("Map staging texture failed: {}", HrToString(hr));
    dxgi_output_dup_->ReleaseFrame();
    return std::nullopt;
  }

  // 构建 Frame 对象
  Frame frame;
  frame.width = width_;
  frame.height = height_;
  frame.stride = static_cast<int32_t>(mapped_resource.RowPitch);
  frame.format = PixelFormat::BGRA32;
  frame.data = static_cast<const uint8_t*>(mapped_resource.pData);
  frame.size = height_ * mapped_resource.RowPitch;

  // 构建元数据
  frame.metadata.timestamp_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();

  frame.metadata.is_key_frame = should_force_key_frame_ || frame_count_ == 0;
  should_force_key_frame_ = false;

  frame.metadata.accumulated_frames = frame_info.AccumulatedFrames;

  // 处理脏区域
  if (config_.enable_dirty_rect) {
    ExtractDirtyRects(frame_info, &frame.metadata);
  } else {
    // 没有脏区域: 整个屏幕被标记为需要编码
    frame.metadata.dirty_rects.push_back({0, 0, width_, height_});
  }

  // 处理移动区域
  if (config_.enable_move_rect) {
    ExtractMoveRects(frame_info, &frame.metadata);
  }

  // 计算脏区域比例
  float dirty_area = 0;
  for (const auto& rect : frame.metadata.dirty_rects) {
    dirty_area += static_cast<float>(rect.Width() * rect.Height());
  }
  frame.metadata.dirty_ratio = dirty_area / (width_ * height_);

  // 更新 FPS 计数
  frame_count_++;
  UpdateFpsCounter();

  // 注意: 此时 staging_texture_ 仍被映射,需要在 ReleaseFrame() 后 Unmap
  last_mapped_resource_ = mapped_resource;

  return frame;
}

void ScreenCapturerDxgi::ReleaseFrame() {
  // Unmap staging texture
  d3d_context_->Unmap(staging_texture_, 0);

  // 释放当前帧给 DXGI 驱动,允许下一帧到来
  if (dxgi_output_dup_) {
    dxgi_output_dup_->ReleaseFrame();
  }
}

void ScreenCapturerDxgi::ForceKeyFrame() {
  should_force_key_frame_ = true;
}

void ScreenCapturerDxgi::GetResolution(int32_t& width, int32_t& height) const {
  width = width_;
  height = height_;
}

PixelFormat ScreenCapturerDxgi::GetPixelFormat() const {
  return PixelFormat::BGRA32;
}

uint32_t ScreenCapturerDxgi::GetCurrentFps() const {
  return current_fps_;
}

bool ScreenCapturerDxgi::IsInitialized() const {
  return is_initialized_;
}

void ScreenCapturerDxgi::Shutdown() {
  Stop();

  if (staging_texture_) {
    staging_texture_->Release();
    staging_texture_ = nullptr;
  }

  if (dxgi_output_dup_) {
    dxgi_output_dup_->Release();
    dxgi_output_dup_ = nullptr;
  }

  if (output_) {
    output_->Release();
    output_ = nullptr;
  }

  if (d3d_context_) {
    d3d_context_->Release();
    d3d_context_ = nullptr;
  }

  if (d3d_device_) {
    d3d_device_->Release();
    d3d_device_ = nullptr;
  }

  is_initialized_ = false;
  ZENREMOTE_INFO("ScreenCapturerDxgi shutdown complete");
}

// ======================== 私有方法 ========================

IDXGIAdapter* ScreenCapturerDxgi::SelectBestAdapter(IDXGIFactory1* factory) {
  IDXGIAdapter* best_adapter = nullptr;
  IDXGIAdapter* current_adapter = nullptr;
  UINT adapter_index = 0;

  while (factory->EnumAdapters(adapter_index, &current_adapter) == S_OK) {
    DXGI_ADAPTER_DESC desc;
    current_adapter->GetDesc(&desc);

    ZENREMOTE_DEBUG("GPU {}: (Dedicated: {} MB)", adapter_index,
                    desc.DedicatedVideoMemory / (1024 * 1024));

    // 优先选择 NVIDIA > AMD > Intel
    // 原因: 独立显卡通常更强,且包含硬件编码器
    if (!best_adapter) {
      best_adapter = current_adapter;
    } else {
      const wchar_t* current_name = desc.Description;
      DXGI_ADAPTER_DESC best_desc;
      best_adapter->GetDesc(&best_desc);
      const wchar_t* best_name = best_desc.Description;

      // NVIDIA
      if (wcsstr(current_name, L"NVIDIA") && !wcsstr(best_name, L"NVIDIA")) {
        best_adapter->Release();
        best_adapter = current_adapter;
      }
      // AMD (次优)
      else if ((wcsstr(current_name, L"AMD") ||
                wcsstr(current_name, L"Radeon")) &&
               !wcsstr(best_name, L"NVIDIA") &&
               !(wcsstr(best_name, L"AMD") || wcsstr(best_name, L"Radeon"))) {
        best_adapter->Release();
        best_adapter = current_adapter;
      }
      // Intel (最后选择)
      else {
        current_adapter->Release();
      }
    }

    adapter_index++;
  }

  ZENREMOTE_INFO("Selected GPU adapter: index={}",
                 adapter_index > 0 ? "multiple" : "0");
  return best_adapter;
}

bool ScreenCapturerDxgi::GetDxgiOutput(uint32_t output_index) {
  // 获取与 D3D 设备关联的 DXGI Device
  IDXGIDevice* dxgi_device = nullptr;
  HRESULT hr = d3d_device_->QueryInterface(IID_PPV_ARGS(&dxgi_device));
  if (FAILED(hr)) {
    ZENREMOTE_ERROR("QueryInterface IDXGIDevice failed: {}", HrToString(hr));
    return false;
  }

  // 获取 DXGI Adapter
  IDXGIAdapter* adapter = nullptr;
  hr = dxgi_device->GetAdapter(&adapter);
  dxgi_device->Release();

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("GetAdapter failed: {}", HrToString(hr));
    return false;
  }

  // 枚举输出(显示器),直到找到目标索引
  IDXGIOutput* current_output = nullptr;
  for (uint32_t i = 0; adapter->EnumOutputs(i, &current_output) == S_OK; ++i) {
    if (i == output_index) {
      DXGI_OUTPUT_DESC desc;
      current_output->GetDesc(&desc);
      ZENREMOTE_INFO("Using DXGI output: index={}, desktop_bounds={}x{}",
                     output_index, desc.DesktopCoordinates.right,
                     desc.DesktopCoordinates.bottom);
      output_ = current_output;
      adapter->Release();
      return true;
    }
    current_output->Release();
  }

  ZENREMOTE_ERROR("Output index {} not found (available: {})", output_index,
                  output_index);
  adapter->Release();
  return false;
}

bool ScreenCapturerDxgi::CreateDesktopDuplication() {
  // 将 IDXGIOutput 转换为 IDXGIOutput1
  IDXGIOutput1* output1 = nullptr;
  HRESULT hr = output_->QueryInterface(IID_PPV_ARGS(&output1));

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("QueryInterface IDXGIOutput1 failed: {}", HrToString(hr));
    ZENREMOTE_ERROR("DuplicateOutput requires Windows 8+");
    return false;
  }

  // 创建 Desktop Duplication
  // 参数:
  //   - d3d_device_: 用于创建 GPU 资源的设备
  //   - ppDesktopImageOut: 输出参数,返回 IDXGIOutputDuplication 对象
  hr = output1->DuplicateOutput(d3d_device_, &dxgi_output_dup_);
  output1->Release();

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("DuplicateOutput failed: {}", HrToString(hr));
    if (hr == E_ACCESSDENIED) {
      ZENREMOTE_ERROR("Access denied - try running as administrator");
    }
    return false;
  }

  ZENREMOTE_INFO("DXGI Desktop Duplication created successfully");
  return true;
}

bool ScreenCapturerDxgi::SetupFrameBuffer() {
  // 获取输出描述(包含分辨率)
  DXGI_OUTPUT_DESC output_desc;
  output_->GetDesc(&output_desc);

  width_ = output_desc.DesktopCoordinates.right -
           output_desc.DesktopCoordinates.left;
  height_ = output_desc.DesktopCoordinates.bottom -
            output_desc.DesktopCoordinates.top;

  // 创建 Staging Texture (CPU 可读)
  // 参数:
  //   - Usage: D3D11_USAGE_STAGING 允许 CPU 读取
  //   - BindFlags: 0 (不与任何管道阶段绑定)
  //   - CPUAccessFlags: D3D11_CPU_ACCESS_READ 允许 CPU 读取
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width_;
  desc.Height = height_;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // BGRA 32-bit
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

  HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &staging_texture_);

  if (FAILED(hr)) {
    ZENREMOTE_ERROR("CreateTexture2D for staging failed: {}", HrToString(hr));
    return false;
  }

  // 计算步长(一行的字节数)
  stride_ = width_ * 4;  // BGRA32 = 4 bytes per pixel

  ZENREMOTE_INFO("Frame buffer setup: {}x{}, stride={}", width_, height_,
                 stride_);
  return true;
}

void ScreenCapturerDxgi::ExtractDirtyRects(
    const DXGI_OUTDUPL_FRAME_INFO& frame_info,
    FrameMetadata* metadata) {
  // 获取脏区域元数据
  UINT metadata_buffer_size = frame_info.TotalMetadataBufferSize;
  if (metadata_buffer_size == 0) {
    // 没有脏区域信息: 整屏标记为脏
    metadata->dirty_rects.push_back({0, 0, width_, height_});
    return;
  }

  std::vector<uint8_t> metadata_buffer(metadata_buffer_size);

  // 获取移动矩形
  UINT move_rects_size = 0;
  HRESULT hr = dxgi_output_dup_->GetFrameMoveRects(
      metadata_buffer.size(),
      reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata_buffer.data()),
      &move_rects_size);

  if (FAILED(hr)) {
    ZENREMOTE_WARN("GetFrameMoveRects failed: {}", HrToString(hr));
    metadata->dirty_rects.push_back({0, 0, width_, height_});
    return;
  }

  // 获取脏矩形
  UINT dirty_rects_size = 0;
  hr = dxgi_output_dup_->GetFrameDirtyRects(
      metadata_buffer.size() - move_rects_size,
      reinterpret_cast<RECT*>(metadata_buffer.data() + move_rects_size),
      &dirty_rects_size);

  if (FAILED(hr)) {
    ZENREMOTE_WARN("GetFrameDirtyRects failed: {}", HrToString(hr));
    metadata->dirty_rects.push_back({0, 0, width_, height_});
    return;
  }

  // 将 RECT 转换为 DirtyRect
  RECT* dirty_rects =
      reinterpret_cast<RECT*>(metadata_buffer.data() + move_rects_size);
  uint32_t dirty_rect_count = dirty_rects_size / sizeof(RECT);

  for (uint32_t i = 0; i < dirty_rect_count; ++i) {
    const RECT& rect = dirty_rects[i];
    metadata->dirty_rects.push_back(
        {rect.left, rect.top, rect.right, rect.bottom});
  }

  ZENREMOTE_DEBUG("Frame has {} dirty rects", metadata->dirty_rects.size());
}

void ScreenCapturerDxgi::ExtractMoveRects(
    const DXGI_OUTDUPL_FRAME_INFO& frame_info,
    FrameMetadata* metadata) {
  UINT metadata_buffer_size = frame_info.TotalMetadataBufferSize;
  if (metadata_buffer_size == 0) {
    return;
  }

  std::vector<uint8_t> metadata_buffer(metadata_buffer_size);

  // 获取移动矩形
  UINT move_rects_size = 0;
  HRESULT hr = dxgi_output_dup_->GetFrameMoveRects(
      metadata_buffer.size(),
      reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata_buffer.data()),
      &move_rects_size);

  if (FAILED(hr)) {
    ZENREMOTE_WARN("GetFrameMoveRects failed: {}", HrToString(hr));
    return;
  }

  DXGI_OUTDUPL_MOVE_RECT* move_rects =
      reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata_buffer.data());
  uint32_t move_rect_count = move_rects_size / sizeof(DXGI_OUTDUPL_MOVE_RECT);

  for (uint32_t i = 0; i < move_rect_count; ++i) {
    const DXGI_OUTDUPL_MOVE_RECT& mr = move_rects[i];
    metadata->move_rects.push_back(
        {{mr.SourcePoint.x, mr.SourcePoint.y, mr.SourcePoint.x,
          mr.SourcePoint.y},
         {mr.DestinationRect.left, mr.DestinationRect.top,
          mr.DestinationRect.right, mr.DestinationRect.bottom}});
  }

  ZENREMOTE_DEBUG("Frame has {} move rects", metadata->move_rects.size());
}

void ScreenCapturerDxgi::UpdateFpsCounter() {
  const int64_t elapsed_ms = fps_timer_.ElapsedMsInt();

  if (elapsed_ms >= 1000) {  // 每秒更新一次
    current_fps_ = static_cast<uint32_t>(frame_count_ * 1000 / elapsed_ms);
    frame_count_ = 0;
    fps_timer_.Reset();
    ZENREMOTE_DEBUG("Capture FPS: {}", current_fps_);
  }
}

void ScreenCapturerDxgi::RecreateOutputDuplication() {
  if (dxgi_output_dup_) {
    dxgi_output_dup_->Release();
    dxgi_output_dup_ = nullptr;
  }

  if (!CreateDesktopDuplication()) {
    ZENREMOTE_ERROR("Failed to recreate output duplication");
  }
}

// ======================== 工厂函数 ========================

std::unique_ptr<ScreenCapturer> CreateScreenCapturer() {
  return std::make_unique<ScreenCapturerDxgi>();
}

}  // namespace zenremote::media::capture
