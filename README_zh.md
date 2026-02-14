**中文** | [English](README.md)

# MCCC - Message-Centric Component Communication

[![CI](https://github.com/DeguiLiu/mccc/actions/workflows/ci.yml/badge.svg)](https://github.com/DeguiLiu/mccc/actions/workflows/ci.yml)
[![Code Coverage](https://github.com/DeguiLiu/mccc/actions/workflows/coverage.yml/badge.svg)](https://github.com/DeguiLiu/mccc/actions/workflows/coverage.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Lock-free MPSC 消息总线，专为安全关键嵌入式系统设计。

> **架构**: MPSC (Multi-Producer, Single-Consumer)。多个生产者线程可安全并发发布消息，单个消费者线程调用 `ProcessBatch()` 处理。如需多消费者，推荐使用多个独立的 `AsyncBus` 实例，每个消费者线程一个。

## 特性

- **Header-only**: 3 个头文件 (`mccc.hpp` + `component.hpp` + `static_component.hpp`)，零外部依赖，纯 C++17
- **Lock-free MPSC**: CAS 原子操作，无锁竞争
- **优先级准入控制**: 系统过载时 HIGH 消息零丢失
- **背压监控**: 四级队列健康状态 (NORMAL/WARNING/CRITICAL/FULL)
- **零堆分配**: Envelope 内嵌 Ring Buffer，热路径无 malloc
- **类型安全**: std::variant 编译期检查
- **MISRA C++ 合规**: 适用于汽车、航空、医疗 (C++17 子集)
- **嵌入式深度优化**: SPSC wait-free、索引缓存、signal fence、BARE_METAL 无锁分发
- **零开销分发**: ProcessBatchWith + CRTP StaticComponent，编译期消息路由

## 性能

> 测试环境: Ubuntu 24.04, GCC 13.3, -O3 -march=native, Intel Xeon Cascadelake 64 vCPU

### 入队吞吐量（编译配置矩阵）

| 配置 | SP | SC | FULL_FEATURED | BARE_METAL | Overhead |
|------|:--:|:--:|:---:|:---:|:---:|
| **MPSC (默认)** | 0 | 0 | 27.7 M/s (36 ns) | 33.0 M/s (30 ns) | 5.8 ns |
| **SPSC** | 1 | 0 | 30.3 M/s (33 ns) | **43.2 M/s (23 ns)** | 9.8 ns |
| **MPSC+单核** | 0 | 1 | 29.2 M/s (34 ns) | 38.2 M/s (26 ns) | 8.1 ns |
| **SPSC+单核** | 1 | 1 | 29.4 M/s (34 ns) | 39.9 M/s (25 ns) | 8.9 ns |

> SP = `MCCC_SINGLE_PRODUCER`, SC = `MCCC_SINGLE_CORE`

### 端到端延迟 (FULL_FEATURED, 10K 样本)

| 配置 | P50 | P95 | P99 | Max |
|------|-----|-----|-----|-----|
| MPSC (默认) | 585 ns | 783 ns | 933 ns | 18 us |
| MPSC+单核 | **310 ns** | **389 ns** | **442 ns** | 17 us |

### 背压准入控制验证 (150K 消息突发)

| 优先级 | 发送 | 丢弃率 |
|--------|------|--------|
| HIGH | 30,000 | **0.0%** |
| MEDIUM | 39,321 | 12.6% |
| LOW | 39,320 | 47.6% |

详见 [docs/benchmark.md](docs/benchmark.md)

## Quick Start

### FetchContent 集成

```cmake
include(FetchContent)
FetchContent_Declare(
  mccc
  GIT_REPOSITORY https://github.com/DeguiLiu/mccc.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(mccc)

target_link_libraries(your_target PRIVATE mccc)
```

### 最小示例

```cpp
#include <mccc/mccc.hpp>
#include <cstdio>

// 1. 定义消息类型
struct SensorData { float temperature; };
struct MotorCmd   { int speed; };

// 2. 定义 PayloadVariant
using MyPayload = std::variant<SensorData, MotorCmd>;
using MyBus = mccc::AsyncBus<MyPayload>;

int main() {
    auto& bus = MyBus::Instance();

    // 3. 订阅
    bus.Subscribe<SensorData>(
        [](const mccc::MessageEnvelope<MyPayload>& env) {
            const auto* data = std::get_if<SensorData>(&env.payload);
            if (data) {
                std::printf("Temperature: %.1f\n", data->temperature);
            }
        });

    // 4. 发布
    bus.Publish(SensorData{36.5f}, /*sender_id=*/1);

    // 5. 消费者处理 (通常在独立线程)
    bus.ProcessBatch();

    return 0;
}
```

## API 参考

```cpp
// 总线单例
AsyncBus<PayloadVariant>::Instance()

// 发布
bool Publish(PayloadVariant&& payload, uint32_t sender_id);
bool PublishWithPriority(PayloadVariant&& payload, uint32_t sender_id,
                         MessagePriority priority);
bool PublishFast(PayloadVariant&& payload, uint32_t sender_id,
                 uint64_t timestamp_us);

// 订阅 / 取消订阅
template<typename T, typename Func>
SubscriptionHandle Subscribe(Func&& callback);
bool Unsubscribe(const SubscriptionHandle& handle);

// 消费
uint32_t ProcessBatch();

// 零开销分发 (编译期, 无锁, 无回调表)
template<typename Visitor>
uint32_t ProcessBatchWith(Visitor&& vis);

// 监控
BackpressureLevel GetBackpressureLevel() const;
BusStatisticsSnapshot GetStatistics() const;
```

完整 API 文档: [docs/api_reference.md](docs/api_reference.md)

## 编译期配置

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `MCCC_QUEUE_DEPTH` | 131072 | 队列深度，必须为 2 的幂 |
| `MCCC_CACHELINE_SIZE` | 64 | 缓存行大小 (字节) |
| `MCCC_SINGLE_PRODUCER` | 0 | SPSC wait-free 快速路径 (1 = 跳过 CAS) |
| `MCCC_SINGLE_CORE` | 0 | 单核模式 (1 = 关闭缓存行对齐 + relaxed + signal_fence)，需同时定义 `MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1` |
| `MCCC_MAX_MESSAGE_TYPES` | 8 | variant 中最大消息类型数 |
| `MCCC_MAX_CALLBACKS_PER_TYPE` | 16 | 每种类型最大回调数 |
| `MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT` | 16 | 每组件最大订阅数 |

嵌入式裁剪示例:
```bash
# 单核单生产者嵌入式 MCU (最大性能)
cmake .. -DCMAKE_CXX_FLAGS="-DMCCC_QUEUE_DEPTH=4096 -DMCCC_SINGLE_PRODUCER=1 -DMCCC_SINGLE_CORE=1 -DMCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1"
```

## 示例

| 示例 | 说明 |
|------|------|
| [simple_demo.cpp](examples/simple_demo.cpp) | 最小可运行示例：订阅、发布、处理消息的完整流程 |
| [priority_demo.cpp](examples/priority_demo.cpp) | 优先级准入控制演示：HIGH/MEDIUM/LOW 三级丢弃策略验证 |
| [hsm_demo.cpp](examples/hsm_demo.cpp) | MCCC + 层次状态机 (HSM) 集成：状态驱动的消息处理模式 |
| [benchmark.cpp](examples/benchmark.cpp) | 性能基准测试：吞吐量、延迟分位数、背压压力、持续吞吐 |

## 测试

171 个测试用例，覆盖:

| 测试文件 | 覆盖内容 |
|---------|---------|
| test_fixed_containers | FixedString 截断/比较, FixedVector 满/空边界 |
| test_ring_buffer | 单/多生产者入队出队正确性, 类型分发 |
| test_priority | 优先级准入 (HIGH 零丢失), BARE_METAL 旁路 |
| test_backpressure | 背压级别切换, 统计计数, 优先级统计 |
| test_subscribe | Subscribe/Unsubscribe 生命周期, Component 析构清理 |
| test_multithread | 4/16/32 生产者压力, 并发订阅/取消订阅 |
| test_stability | 吞吐量稳定性 (10 轮统计), 持续吞吐, 入队延迟分位数 |
| test_edge_cases | 队列满恢复, 错误回调, NO_STATS 模式, 性能模式切换 |
| test_copy_move | CopyMoveCounter 零拷贝验证, FixedVector 移动语义 |
| test_fixed_function | FixedFunction SBO 类型擦除, 移动语义, 空调用 |
| test_visitor_dispatch | ProcessBatchWith 分发, 吞吐量对比 |
| test_static_component | CRTP StaticComponent, HasHandler trait 检测 |

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
./tests/mccc_tests        # 运行全部测试
./examples/mccc_benchmark  # 性能基准测试
```

## 仓库结构

```
mccc-bus/
├── include/mccc/
│   ├── mccc.hpp              # 核心: FixedString, FixedVector, FixedFunction, AsyncBus, Priority
│   ├── component.hpp         # Component<PayloadVariant> - 运行时动态订阅组件 (可选)
│   └── static_component.hpp  # StaticComponent<Derived, PayloadVariant> - CRTP 零开销组件 (可选)
├── examples/
│   ├── example_types.hpp   # 示例消息类型定义
│   ├── simple_demo.cpp     # 最小使用示例
│   ├── priority_demo.cpp   # 优先级准入演示
│   ├── hsm_demo.cpp        # HSM 状态机集成
│   └── benchmark.cpp       # 性能基准测试
├── tests/                  # 单元测试 (Catch2, 171 用例)
├── extras/                 # HSM, DMA BufferPool, DataToken, 日志宏
├── docs/                   # 设计文档、性能报告、竞品分析、API 参考
└── CMakeLists.txt
```

## 文档

- [API 参考](docs/api_reference.md) - 完整接口文档（中文）
- [架构设计](docs/design.md) - Ring Buffer, CAS 协议, 优先级准入, 零堆分配容器
- [性能报告](docs/benchmark.md) - 吞吐量, 延迟, 背压测试数据
- [竞品分析](docs/competitive_analysis.md) - 与 eventpp, EnTT, sigslot, ZeroMQ, QP/C++ 对标

## License

MIT
