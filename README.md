[中文](README_zh.md) | **English**

# MCCC - Message-Centric Component Communication

Lock-free MPSC message bus designed for safety-critical embedded systems.

> **Architecture**: MPSC (Multi-Producer, Single-Consumer). Multiple producer threads can safely publish messages concurrently; a single consumer thread calls `ProcessBatch()` for processing. For multiple consumers, use separate `AsyncBus` instances, one per consumer thread.

## Features

- **Header-only**: 2 header files (`mccc.hpp` + `component.hpp`), zero external dependencies, pure C++17
- **Lock-free MPSC**: CAS atomic operations, no lock contention
- **Priority admission control**: Zero message loss for HIGH priority under system overload
- **Backpressure monitoring**: Four-level queue health status (NORMAL/WARNING/CRITICAL/FULL)
- **Zero heap allocation**: Envelopes embedded in Ring Buffer, no malloc on hot path
- **Type-safe**: Compile-time checking via std::variant
- **MISRA C++ compliant**: Suitable for automotive, aerospace, and medical applications (C++17 subset)
- **Deep embedded optimization**: SPSC wait-free, index caching, signal fence, BARE_METAL lock-free dispatch

## Performance

> Test environment: Ubuntu 24.04, GCC 13.3, -O3 -march=native, Intel Xeon Cascadelake 64 vCPU

### Enqueue Throughput (Compile Configuration Matrix)

| Configuration | SP | SC | FULL_FEATURED | BARE_METAL | Overhead |
|---------------|:--:|:--:|:---:|:---:|:---:|
| **MPSC (default)** | 0 | 0 | 27.7 M/s (36 ns) | 33.0 M/s (30 ns) | 5.8 ns |
| **SPSC** | 1 | 0 | 30.3 M/s (33 ns) | **43.2 M/s (23 ns)** | 9.8 ns |
| **MPSC+single-core** | 0 | 1 | 29.2 M/s (34 ns) | 38.2 M/s (26 ns) | 8.1 ns |
| **SPSC+single-core** | 1 | 1 | 29.4 M/s (34 ns) | 39.9 M/s (25 ns) | 8.9 ns |

> SP = `MCCC_SINGLE_PRODUCER`, SC = `MCCC_SINGLE_CORE`

### End-to-End Latency (FULL_FEATURED, 10K samples)

| Configuration | P50 | P95 | P99 | Max |
|---------------|-----|-----|-----|-----|
| MPSC (default) | 585 ns | 783 ns | 933 ns | 18 us |
| MPSC+single-core | **310 ns** | **389 ns** | **442 ns** | 17 us |

### Backpressure Admission Control Verification (150K message burst)

| Priority | Sent | Drop Rate |
|----------|------|-----------|
| HIGH | 30,000 | **0.0%** |
| MEDIUM | 39,321 | 12.6% |
| LOW | 39,320 | 47.6% |

See [docs/benchmark.md](docs/benchmark.md) for details.

## Quick Start

### FetchContent Integration

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

### Minimal Example

```cpp
#include <mccc/mccc.hpp>
#include <cstdio>

// 1. Define message types
struct SensorData { float temperature; };
struct MotorCmd   { int speed; };

// 2. Define PayloadVariant
using MyPayload = std::variant<SensorData, MotorCmd>;
using MyBus = mccc::AsyncBus<MyPayload>;

int main() {
    auto& bus = MyBus::Instance();

    // 3. Subscribe
    bus.Subscribe<SensorData>(
        [](const mccc::MessageEnvelope<MyPayload>& env) {
            const auto* data = std::get_if<SensorData>(&env.payload);
            if (data) {
                std::printf("Temperature: %.1f\n", data->temperature);
            }
        });

    // 4. Publish
    bus.Publish(SensorData{36.5f}, /*sender_id=*/1);

    // 5. Consumer processing (typically in a dedicated thread)
    bus.ProcessBatch();

    return 0;
}
```

## API Reference

```cpp
// Bus singleton
AsyncBus<PayloadVariant>::Instance()

// Publish
bool Publish(PayloadVariant&& payload, uint32_t sender_id);
bool PublishWithPriority(PayloadVariant&& payload, uint32_t sender_id,
                         MessagePriority priority);
bool PublishFast(PayloadVariant&& payload, uint32_t sender_id,
                 uint64_t timestamp_us);

// Subscribe / Unsubscribe
template<typename T, typename Func>
SubscriptionHandle Subscribe(Func&& callback);
bool Unsubscribe(const SubscriptionHandle& handle);

// Consume
uint32_t ProcessBatch();

// Monitoring
BackpressureLevel GetBackpressureLevel() const;
BusStatisticsSnapshot GetStatistics() const;
```

Full API documentation: [docs/api_reference.md](docs/api_reference.md)

## Compile-Time Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `MCCC_QUEUE_DEPTH` | 131072 | Queue depth, must be a power of 2 |
| `MCCC_CACHELINE_SIZE` | 64 | Cache line size (bytes) |
| `MCCC_SINGLE_PRODUCER` | 0 | SPSC wait-free fast path (1 = skip CAS) |
| `MCCC_SINGLE_CORE` | 0 | Single-core mode (1 = disable cache line alignment + relaxed + signal_fence), requires `MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1` |
| `MCCC_MAX_MESSAGE_TYPES` | 8 | Maximum message types in variant |
| `MCCC_MAX_CALLBACKS_PER_TYPE` | 16 | Maximum callbacks per type |
| `MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT` | 16 | Maximum subscriptions per component |

Embedded trimming example:
```bash
# Single-core single-producer embedded MCU (maximum performance)
cmake .. -DCMAKE_CXX_FLAGS="-DMCCC_QUEUE_DEPTH=4096 -DMCCC_SINGLE_PRODUCER=1 -DMCCC_SINGLE_CORE=1 -DMCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1"
```

## Examples

| Example | Description |
|---------|-------------|
| [simple_demo.cpp](examples/simple_demo.cpp) | Minimal runnable example: complete subscribe, publish, and process workflow |
| [priority_demo.cpp](examples/priority_demo.cpp) | Priority admission control demo: HIGH/MEDIUM/LOW three-level drop policy verification |
| [hsm_demo.cpp](examples/hsm_demo.cpp) | MCCC + Hierarchical State Machine (HSM) integration: state-driven message processing |
| [benchmark.cpp](examples/benchmark.cpp) | Performance benchmark: throughput, latency percentiles, backpressure stress, sustained throughput |

## Testing

70 test cases, 203 assertions, covering:

| Test File | Coverage |
|-----------|----------|
| test_fixed_containers | FixedString truncation/comparison, FixedVector full/empty boundary |
| test_ring_buffer | Single/multi-producer enqueue/dequeue correctness, type dispatch |
| test_priority | Priority admission (HIGH zero-loss), BARE_METAL bypass |
| test_backpressure | Backpressure level transitions, statistics counting, priority statistics |
| test_subscribe | Subscribe/Unsubscribe lifecycle, Component destructor cleanup |
| test_multithread | 4/16/32 producer stress, concurrent subscribe/unsubscribe |
| test_stability | Throughput stability (10-round statistics), sustained throughput, enqueue latency percentiles |
| test_edge_cases | Queue-full recovery, error callbacks, NO_STATS mode, performance mode switching |
| test_copy_move | CopyMoveCounter zero-copy verification, FixedVector move semantics |

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
./tests/mccc_tests        # Run all tests
./examples/mccc_benchmark  # Performance benchmark
```

## Repository Structure

```
mccc/
├── include/mccc/
│   ├── mccc.hpp           # Core: FixedString, FixedVector, MessageEnvelope, AsyncBus, Priority
│   └── component.hpp      # Component<PayloadVariant> - Safe component base class (optional)
├── examples/
│   ├── example_types.hpp   # Example message type definitions
│   ├── simple_demo.cpp     # Minimal usage example
│   ├── priority_demo.cpp   # Priority admission demo
│   ├── hsm_demo.cpp        # HSM state machine integration
│   └── benchmark.cpp       # Performance benchmark
├── tests/                  # Unit tests (Catch2, 70 cases)
├── extras/                 # HSM, DMA BufferPool, DataToken, logging macros
├── docs/                   # Design docs, performance reports, competitive analysis, API reference
└── CMakeLists.txt
```

## Documentation

- [API Reference](docs/api_reference.md) - Complete interface documentation (Chinese)
- [Architecture Design](docs/design.md) - Ring Buffer, CAS protocol, priority admission, zero-heap containers
- [Performance Report](docs/benchmark.md) - Throughput, latency, backpressure test data
- [Competitive Analysis](docs/competitive_analysis.md) - Comparison with eventpp, EnTT, sigslot, ZeroMQ, QP/C++

## License

MIT
