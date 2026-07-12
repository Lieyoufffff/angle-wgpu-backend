# ANGLE WebGPU Backend Prototype

基于 [ANGLE](https://chromium.googlesource.com/angle/angle) 实现的 WebGPU 后端原型，将 WebGL / OpenGL ES API 调用翻译到 WebGPU (Dawn) 上执行。

## 项目目标

3D AIGC 生成的资产需要在浏览器中实时渲染和交互。WebGPU 提供了比 WebGL 更现代、更高效的 GPU 访问方式，但大量现有 WebGL 应用和 benchmark 无法直接迁移。本项目通过 ANGLE 的翻译层架构，使现有 WebGL workload 能够无缝运行在 WebGPU backend 之上。

## 基于上游

基于 ANGLE upstream commit `c053bf85793b` (Metal: Allow prebuilding internal shaders for iOS)。

上游已有 WebGPU backend 的基本骨架（`src/libANGLE/renderer/wgpu/`），但大量功能是 `UNIMPLEMENTED()` 占位符。本项目补全了关键缺失，使完整的渲染管线可用。

## 核心改动

相对于上游 ANGLE，本项目修改了 22 个文件（+411/-231 行），集中在 `src/libANGLE/renderer/wgpu/`：

| 改动 | 文件 | 说明 |
|------|------|------|
| 同步设备创建 | `DisplayWgpu.cpp`, `wgpu_proc_utils.*` | 用 `dawn::native` 同步 API 替换异步 callback 路径，修复 headless 环境挂起 |
| TriangleFan 模拟 | `ContextWgpu.cpp`, `VertexArrayWgpu.*` | 将 fan 顶点展开为 triangle list 索引，WebGPU 不支持此图元 |
| Sampler 缓存 | `ContextWgpu.*`, `ProgramExecutableWgpu.cpp` | 基于 hash 的 sampler/bind group 缓存，避免每帧重复创建 |
| Indirect Draw | `ContextWgpu.cpp` | CPU 端读取 indirect command 转发到 instanced draw |
| MultiDraw | `ContextWgpu.cpp` | 8 个 multiDraw 变体，调用 ANGLE 通用工具函数 |
| Blend ConstantAlpha | `ContextWgpu.cpp`, `wgpu_utils.cpp` | WebGPU 无 ConstantAlpha factor，用 D3D11 模式 rewrite blend color |
| 动态 Offset 支持 | `wgpu_command_buffer.*` | SetBindGroup 支持最多 3 个 dynamic offsets |
| Buffer Usage 修正 | `BufferWgpu.cpp` | 为所有 GL buffer binding 类型映射正确的 WebGPU usage flags |
| 各类 Fallback | `wgpu_utils.cpp`, `wgpu_helpers.cpp` | CullFaceMode、CLAMP_TO_BORDER、shadow sampler 等合理降级 |

## 测试结果

| 测试 | 结果 |
|------|------|
| 独立 E2E 功能验证 | **8/10 通过** (2 项已知限制: depth test、buffer re-upload) |
| 跨后端像素对比 | 6 组场景, 4 组 100% 像素一致, 2 组差异仅来自深度路径缺失 |
| test_3d_cube | PASS — 3D 旋转立方体 (MVP + indexed draw) |
| test_e2e_render | PASS — 端到端渲染正确性验证 |
| test_device_sync | PASS — WebGPU device 同步创建 |
| MotionMark 1.3.1 | 8 项子测试全部通过，总分达到 Vulkan 后端 94.5% |

### MotionMark 1.3.1 全套件对比 (WebGPU vs Vulkan)

| 子测试 | WebGPU | Vulkan | 比率 |
|--------|--------|--------|------|
| Multiply | 1529.46 | 1746.33 | 0.88x |
| Canvas Arcs | 483.77 | 668.45 | 0.72x |
| Leaves | 452.42 | 463.51 | 0.98x |
| Paths | 1823.56 | 1777.55 | 1.03x |
| Canvas Lines | 2416.65 | 2639.45 | 0.92x |
| Images | 282.08 | 279.77 | 1.01x |
| Design | 219.86 | 197.93 | 1.11x |
| Suits | 557.72 | 572.62 | 0.97x |
| **Overall (geometric mean)** | **689.41** | **729.83** | **0.94x** |

### Performance Benchmarks

| Benchmark | 结果 |
|-----------|------|
| Pipeline Cache | 冷启动 43.7ms → 热命中 1.9ms (22.8x 加速) |
| Sampler Cache | 1000 次创建：无缓存 3.57ms → 缓存 0.025μs (35681x) |
| Uniform Ring Buffer | 1000 draw calls：逐次分配 8.66ms → ring buffer 0.009ms (1008x) |

## 目录结构

```
├── src/libANGLE/renderer/wgpu/   # WebGPU backend 核心实现 (~18,800 行)
│   ├── ContextWgpu.*             # GL context → WebGPU 映射, draw call 派发
│   ├── DisplayWgpu.*             # EGL display, device 创建
│   ├── BufferWgpu.*              # buffer 管理
│   ├── TextureWgpu.*             # texture 管理
│   ├── FramebufferWgpu.*         # FBO
│   ├── VertexArrayWgpu.*         # 顶点属性, 索引流化, TriangleFan 模拟
│   ├── ProgramExecutableWgpu.*   # shader 执行, uniform, sampler bind group
│   ├── wgpu_command_buffer.*     # 命令录制与回放
│   ├── wgpu_pipeline_state.*     # pipeline 创建与缓存
│   ├── wgpu_proc_utils.*         # Dawn proc 初始化, 同步 device 创建
│   ├── wgpu_format_utils.*       # GL↔WebGPU 格式映射
│   └── wgpu_helpers.*            # 工具函数
├── test_3d_cube.cpp              # 3D 立方体 demo
├── test_e2e_render.cpp           # 端到端渲染测试
├── test_realtime_cube.cpp        # 实时渲染 + FPS
├── test_device_sync.cpp          # device 创建验证
├── benchmark_pipeline_cache.cpp  # pipeline 缓存 benchmark
├── benchmark_sampler_cache.cpp   # sampler 缓存 benchmark
├── benchmark_uniform.cpp         # uniform 分配 benchmark
├── benchmark_frame_stability.cpp # 帧稳定性 benchmark
├── third_party/MotionMark/       # MotionMark 1.3.1 浏览器图形基准测试 (submodule)
│   └── MotionMark/
│       ├── developer.html        # 开发者测试入口
│       └── tests/                # 子测试 (Multiply, Canvas Arcs, Leaves 等)
└── BUILD.gn                      # 构建定义 (含自定义 target)
```

## 构建指南

### 前置依赖

- Linux x86_64 (Ubuntu 22.04 tested)
- `depot_tools` (提供 gn)
- `ninja` (位于 `third_party/ninja/ninja`)
- Xvfb 或 VNC (用于测试的 display server)

### 获取完整源码

本仓库不包含 `third_party/`、`build/`、`tools/` 等大型依赖目录。完整构建需要：

```bash
# 1. 克隆本仓库
git clone https://github.com/Lieyoufffff/angle-wgpu-backend.git
cd angle-wgpu-backend

# 2. 获取依赖 (使用 depot_tools)
gclient sync

# 3. 或者：在已有的 ANGLE checkout 上应用本项目改动
cd /path/to/angle
git remote add wgpu https://github.com/Lieyoufffff/angle-wgpu-backend.git
git fetch wgpu
git diff HEAD..wgpu/main -- src/ BUILD.gn | git apply
```

### 配置与编译

```bash
# 生成构建配置
gn gen out/Release --args='
  angle_enable_wgpu = true
  angle_enable_vulkan = false
  angle_enable_gl = false
  angle_enable_d3d9 = false
  angle_enable_d3d11 = false
  angle_enable_metal = false
  angle_enable_null = false
  angle_enable_swiftshader = true
  dawn_use_swiftshader = true
  is_debug = false
  target_cpu = "x64"
'

# 编译
ninja -C out/Release angle_end2end_tests test_3d_cube test_e2e_render test_realtime_cube
```

### 运行测试

```bash
cd out/Release
export LD_LIBRARY_PATH=.
export DISPLAY=:1  # 或启动 Xvfb: Xvfb :1 &

# ANGLE end2end tests
./angle_end2end_tests --gtest_filter='*ES2_WebGPU'

# 3D cube demo (输出 PPM 帧)
./test_3d_cube

# 端到端渲染验证
./test_e2e_render

# 实时渲染 (需要窗口环境)
./test_realtime_cube
```

> **注意：** 需要注释掉 `src/tests/angle_end2end_tests_expectations.txt` 第 2342 行
> `517972806 WGPU LINUX NVIDIA : * = SKIP`，否则所有 WebGPU 测试会被跳过（上游 CI 配置）。

## 技术设计要点

### State Machine → Pipeline Cache

WebGL 基于细粒度状态机，WebGPU 基于不可变 Pipeline State Object。本实现将 blend/depth/rasterization 状态 + shader 组合为 hash key，通过多级缓存避免重复创建 pipeline（实测 22.8x 加速）。

### Shader Translation

GLSL ES → SPIR-V → WGSL 的转换路径利用 ANGLE 内置 compiler (`src/compiler`) + Tint。Uniform block 和 binding layout 由 `ProgramExecutableWgpu` 自动分配。

### Resource Lifetime

WebGPU 要求资源在 GPU 使用完成前不被释放。本实现通过：
- Command buffer 引用计数（`GetReferencedObject`）防止提交期间释放
- Ring buffer 策略减少 per-draw 分配
- Deferred destruction 在 command submit 后释放

## License

与 ANGLE 相同，BSD-style license。详见 [LICENSE](LICENSE)。
