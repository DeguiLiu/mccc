# MCCC 基准测试方法论报告

本文档详细描述了 MCCC (Message-Centric Component Communication) 性能测试套件的实施细节与设计原理。

## 1. 测试体系设计哲学

本基准测试套件的设计遵循三大原则：

1.  **公平性 (Fairness)**: 通过 BARE_METAL 模式剥离非核心功能开销，确保基准对比有意义。
2.  **严谨性 (Rigor)**: 摒弃单次运行的测试方式，采用多轮次、预热、分位数统计（P99/P95）的科学统计方法。
3.  **可复现性 (Reproducibility)**: 所有测试参数（轮次、消息大小、队列深度）均通过配置硬编码，确保不同环境下的一致性。

---

## 2. BARE_METAL 与 FULL_FEATURED 模式

MCCC 原生包含优先级、背压和统计功能。直接将全功能模式与纯队列性能对比是不公平的。为此，我们引入了 `PerformanceMode`。

| 模式 | MCCC 内部行为 (代码级验证) | 用途 |
| :--- | :--- | :--- |
| **FULL_FEATURED** | 完整执行原子计数、优先级阈值检查、背压状态机更新。 | 生产环境基线 |
| **BARE_METAL** | **完全跳过**优先级判断、背压检查和原子统计。仅保留核心的 `CAS` 入队操作和 RingBuffer 索引计算。 | 纯队列性能基线 |

**代码证据** (`include/mccc/message_bus.hpp`):
```cpp
// 在 PublishInternal 中
const bool bare_metal = (performance_mode_ == PerformanceMode::BARE_METAL);
// ...
if (!bare_metal) {
    // 昂贵的优先级检查被跳过
    uint32_t threshold = GetThresholdForPriority(priority);
    // ...
}
```

---

## 3. 统计方法

所有基准测试均采用相同的统计口径：

*   **多轮次执行**: 3 轮预热 + 10 轮正式测试。
*   **指标对齐**: 均计算并报告 Mean, StdDev, Min, Max, P50, P95, P99。
*   **消除偶然性**: 避免了"特定时刻系统抖动"对单次测试结果的误导。

### 统计模型

*   **平均值 (Mean)**: 反映整体趋势。
*   **标准差 (StdDev)**: 衡量性能抖动（Jitter）。标准差越小，系统确定性越高。
*   **P99 (99th Percentile)**: 关键指标，反映"最坏情况"下的性能，对于实时系统至关重要。

```cpp
// 核心统计算法
Statistics calculate_statistics(const std::vector<double>& data) {
    // 先排序，后取分位数
    std::sort(sorted_data.begin(), sorted_data.end());
    stats.p99 = sorted_data[n * 99 / 100];
    // ...
}
```

---

## 4. 关键测试场景实施细节

### 4.1 端到端 (E2E) 延迟测试

E2E 延迟测量的是从 `Publish()` 调用开始，到消费者 `Callback` 第一行代码执行之间的时间差。为了精确测量纳秒级延迟，我们使用了 **原子屏障 (Atomic Barrier)** 技术。

**实现逻辑**:
1.  **生产者**: 记录 `publish_ts`，调用 `Publish()`，并在原子变量 `measurement_ready` 上自旋等待（带超时）。
2.  **消费者**: 在回调函数的**第一行**获取 `callback_ts`，并设置 `measurement_ready = true`。
3.  **计算**: `Latency = callback_ts - publish_ts`。

这种方法避免了在每个消息中打时间戳带来的内存带宽压力，而是采用"采样"方式（每隔一定间隔进行一次精确测量），从而获得极高精度的无负载延迟数据。

### 4.2 背压 (Backpressure) 压力测试

为了验证优先级丢包逻辑的正确性，依靠自然消费堆积往往不可控。我们采用了 **消费者暂停 (Consumer Pause)** 策略：

1.  **暂停消费者**: 通过原子标志位挂起消费者线程。
2.  **突发写入**: 推送超过队列容量（>128K）的消息混合流（20% High, 30% Medium, 50% Low）。
3.  **恢复与计算**: 恢复消费者，统计各优先级的实际丢包率。

**预期结果验证**:
*   Low Priority 应首先被丢弃 (Drop Rate Highest)。
*   High Priority 应最后被丢弃 (Drop Rate Lowest/Zero)。
*   此测试确保了 MCCC 的 `Safety` 特性在极端工况下的表现。

### 4.3 测量开销的最小化

在测量高频消息（如 20M msg/s）时，`std::chrono::high_resolution_clock::now()` 本身会带来显著开销（约 20-50ns）。

*   **策略**: 在吞吐量测试循环中，采用 **时间戳缓存策略**（每 100 条消息更新一次时间戳，或在仅吞吐量测试中不通过消息传递时间戳），确保测试测量的是总线性能，而非系统时钟调用的性能。

---

## 5. 编译环境规范

为了保证测试的有效性，所有测试必须在统一的编译配置下运行：

*   **Standard**: C++17
*   **Flags**: `-O3 -march=native -faligned-new`
*   **Alignment**: `alignas(64)` 用于防止 False Sharing (伪共享)。

---

## 6. 复现测试

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# 运行 MCCC 性能测试
./examples/mccc_benchmark
```

---

## 7. 总结

本测试套件不仅仅是一个跑分工具，更是一个验证 MCCC 设计目标（低延迟、确定性、优先级安全）的系统化框架。通过 `BARE_METAL` 模式的引入和统计方法的统一，确立了严谨的性能评估标准。
