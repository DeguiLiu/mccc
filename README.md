# MCCC - Message-Centric Component Communication

Lock-free MPSC 消息总线，专为安全关键嵌入式系统设计。

## 特性

- Header-only: 3 个头文件，零外部依赖，纯 C++17
- Lock-free MPSC: CAS 原子操作，无锁竞争
- 优先级准入控制: 系统过载时 HIGH 消息零丢失
- 背压监控: 四级队列健康状态 (NORMAL/WARNING/CRITICAL/FULL)
- 零堆分配: Envelope 内嵌 Ring Buffer，热路径无 malloc
- 类型安全: std::variant 编译期检查
- MISRA C++ 合规: 适用于汽车、航空、医疗

## 性能

| 模式 | 吞吐量 | 入队延迟 | E2E P99 |
|------|--------|---------|---------|
| BARE_METAL | 18.7 M/s | 54 ns | - |
| FULL_FEATURED | 5.8 M/s | 172 ns | 449 ns |

> 测试环境: Ubuntu 24.04, GCC 13.3, -O3 -march=native. 详见 [docs/benchmark.md](docs/benchmark.md).

## Quick Start

### FetchContent 集成

```cmake
include(FetchContent)
FetchContent_Declare(
  mccc
  GIT_REPOSITORY https://github.com/your-org/mccc-bus.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(mccc)

target_link_libraries(your_target PRIVATE mccc::mccc)
```

### 最小示例

```cpp
#include <mccc/component.hpp>
#include <cstdio>
#include <thread>
#include <variant>

// 1. 定义消息类型
struct SensorData { float temperature; };
struct MotorCmd   { int speed; };

// 2. 定义 PayloadVariant
using MyPayload = std::variant<SensorData, MotorCmd>;
using MyBus = mccc::AsyncBus<MyPayload>;

int main() {
    auto& bus = MyBus::Instance();

    // 3. 订阅
    bus.template Subscribe<SensorData>(
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

// 订阅 / 取消订阅
template<typename T, typename Func>
SubscriptionHandle Subscribe(Func&& callback);
bool Unsubscribe(const SubscriptionHandle& handle);

// 消费
uint32_t ProcessBatch();

// 监控
BackpressureLevel GetBackpressureLevel() const;
BusStatisticsSnapshot GetStatistics() const;
```

## 编译期配置

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `MCCC_QUEUE_DEPTH` | 131072 | 队列深度，必须为 2 的幂 |
| `MCCC_CACHELINE_SIZE` | 64 | 缓存行大小 (字节) |
| `MCCC_CACHE_COHERENT` | 1 | 启用缓存行对齐 (0 = 关闭，节省 RAM) |
| `MCCC_MAX_MESSAGE_TYPES` | 8 | variant 中最大消息类型数 |
| `MCCC_MAX_CALLBACKS_PER_TYPE` | 16 | 每种类型最大回调数 |
| `MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT` | 16 | 每组件最大订阅数 |

嵌入式裁剪示例:
```bash
cmake .. -DCMAKE_CXX_FLAGS="-DMCCC_QUEUE_DEPTH=4096 -DMCCC_CACHE_COHERENT=0"
```

## 仓库结构

```
mccc-bus/
├── include/mccc/
│   ├── protocol.hpp       # FixedString, FixedVector, MessageEnvelope, Priority
│   ├── message_bus.hpp    # AsyncBus<PayloadVariant> - Lock-free Ring Buffer
│   └── component.hpp      # Component<PayloadVariant> - 安全组件基类
├── examples/
│   ├── example_types.hpp   # 示例消息类型定义
│   ├── simple_demo.cpp     # 最小使用示例
│   ├── priority_demo.cpp   # 优先级准入演示
│   ├── hsm_demo.cpp        # HSM 状态机集成
│   └── benchmark.cpp       # 性能基准测试
├── tests/                  # 单元测试
├── extras/                 # HSM, DMA BufferPool, DataToken, 日志宏
├── docs/                   # 设计文档、性能报告、竞品分析
└── CMakeLists.txt
```

## 文档

- [架构设计](docs/design.md) - Ring Buffer, CAS 协议, 优先级准入, 零堆分配容器
- [性能报告](docs/benchmark.md) - 吞吐量, 延迟, 背压测试数据
- [竞品分析](docs/competitive_analysis.md) - 与 7 个开源 lock-free 队列对比
- [优化总结](docs/optimization.md) - OPT-1~8 实施详情与 MISRA 修复
- [测试方法论](docs/methodology.md) - E2E 延迟测量, 统计方法, 编译环境

## License

MIT
