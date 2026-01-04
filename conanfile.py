from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout

class ZenPlayConan(ConanFile):
    name = "ZenPlay"
    settings = "os", "compiler", "build_type", "arch"
    #options = {
    #    "shared": [True, False],
    #    "fPIC": [True, False]
    #}

    def configure(self):
        # ==================== Qt 配置 ====================
        self.options["qt"].shared = True
        self.options["qt"].qttools = True
        
        # ==================== FFmpeg 硬件加速配置 ====================
        # Windows 硬件解码（必需）
        self.options["ffmpeg"].with_d3d11va = True        # D3D11 视频加速
        self.options["ffmpeg"].with_dxva2 = True          # DXVA2 兼容性
        
        # NVIDIA 硬件编解码（强烈推荐）
        self.options["ffmpeg"].with_cuda = True           # CUDA 支持
        self.options["ffmpeg"].with_cuvid = True          # NVIDIA 硬件解码
        self.options["ffmpeg"].with_nvenc = True          # NVIDIA 硬件编码
        
        # Intel 硬件编解码（推荐）
        self.options["ffmpeg"].with_libmfx = True         # Intel QSV
        
        # AMD 硬件编码（推荐）
        self.options["ffmpeg"].with_amf = True            # AMD AMF
        
        # 软件编码器（必需，作为回退）
        self.options["ffmpeg"].with_libx264 = True        # H.264 软件编码
        self.options["ffmpeg"].with_libx265 = True        # HEVC 软件编码
        
        # 其他 FFmpeg 选项
        self.options["ffmpeg"].shared = False             # 静态链接
        self.options["ffmpeg"].with_programs = False      # 不需要 ffmpeg 命令行工具
        
        
    def requirements(self):
        self.requires("nlohmann_json/[^3.12.0]")
        self.requires("qt/6.7.3")
        self.requires("sdl/2.32.2")
        self.requires("ffmpeg/7.1.1")
        self.requires("spdlog/[^1.15.1]")
        self.requires("gtest/1.17.0")
        self.requires("fmt/[^11.1.3]")
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()