# MCCC 优化总结

> 涉及文件: 3 个核心头文件 + extras

---

## 一、性能优化 (OPT)

### OPT-1: 队列深度编译期可配置

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- 新增 `MCCC_QUEUE_DEPTH` 宏 (默认 131072 = 128K)
- 嵌入式系统可通过 `-DMCCC_QUEUE_DEPTH=4096` 缩小内存占用
- 编译期 `static_assert` 确保必须是 2 的幂

### OPT-2: MessageEnvelope 内嵌 RingBufferNode

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- **之前**: `std::make_shared<MessageEnvelope>` 每次 Publish 时堆分配
- **现在**: `MessageEnvelope<PayloadVariant>` 直接嵌入 `RingBufferNode` 结构体
- **效果**: Publish 热路径零堆分配

```cpp
// Before
struct RingBufferNode {
    std::atomic<uint32_t> sequence;
    std::shared_ptr<MessageEnvelope> envelope;  // 堆分配
};

// After
struct RingBufferNode {
    std::atomic<uint32_t> sequence;
    MessageEnvelope<PayloadVariant> envelope;  // 直接嵌入，零堆分配
};
```

### OPT-3: 缓存行对齐编译期可配置

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- 新增 `MCCC_CACHELINE_SIZE` (默认 64) 和 `MCCC_CACHE_COHERENT` (默认 1) 宏
- 单核 MCU 可通过 `-DMCCC_CACHE_COHERENT=0` 关闭对齐填充，节省 RAM

### OPT-4: 热路径无异常

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp), [include/mccc/component.hpp](../include/mccc/component.hpp)

- Publish/ProcessOne/DispatchMessage 全部标记 `noexcept`
- `SubscribeSafe` 使用 `std::get_if` 替代 `std::get`，避免异常
- 兼容 `-fno-exceptions` 编译

### OPT-7: 固定回调表替代 unordered_map

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- **之前**: `std::unordered_map<std::type_index, vector<callback>>` -- 堆分配 + hash 查找
- **现在**: `std::array<CallbackSlot, MCCC_MAX_MESSAGE_TYPES>` -- 栈分配 + O(1) 索引
- `VariantIndex<T, PayloadVariant>` 编译期计算类型索引，替代 `typeid()`

---

## 二、MISRA C++ 合规修复

### R-1: `performance_mode_` 原子化

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- **之前**: `PerformanceMode performance_mode_` -- 多线程读写无同步
- **现在**: `std::atomic<PerformanceMode> performance_mode_`
- 写端: `store(mode, relaxed)` / 读端: `load(relaxed)`

### R-2: `error_callback_` 原子化

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- **之前**: `SetErrorCallback()` 加锁写，`ReportError()` 无锁读 -- 数据竞争
- **现在**: `std::atomic<ErrorCallback> error_callback_{nullptr}`
- 写端: `store(callback, release)` / 读端: `load(acquire)`

### R-4: DispatchMessage 锁持有分析

**文件**: [include/mccc/message_bus.hpp](../include/mccc/message_bus.hpp)

- 分析了三种方案: (1) 锁内调用 (2) 指针快照 (3) std::function 拷贝
- 指针快照有 use-after-free 风险 (回调中 Unsubscribe)
- std::function 拷贝有堆分配风险
- **结论**: 保持锁内调用，作为安全-性能权衡的正确选择

### R-6: DataToken 热路径堆分配消除

**文件**: [extras/data_token.hpp](../extras/data_token.hpp), [extras/buffer_pool.hpp](../extras/buffer_pool.hpp), [extras/data_token.cpp](../extras/data_token.cpp)

- **之前**: `ITokenReleaser` 虚基类 + `std::unique_ptr<DMABufferReleaser>` -- 每次 `Borrow()` 堆分配
- **现在**: `ReleaseCallback` 函数指针 + `void* context` + `uint32_t buffer_index`
- **效果**: 完全消除虚表 + 堆分配

```cpp
// Before: 虚基类 + unique_ptr (每次 Borrow 堆分配)
class ITokenReleaser { virtual void Release() = 0; };
DataToken(ptr, len, ts, std::make_unique<DMABufferReleaser>(pool, idx));

// After: 函数指针 (零堆分配)
using ReleaseCallback = void (*)(void* context, uint32_t index) noexcept;
DataToken(ptr, len, ts, &DMABufferPool::ReleaseBuffer, this, idx);
```

### DMA 对齐编译期可配置

**文件**: [extras/buffer_pool.hpp](../extras/buffer_pool.hpp), [extras/data_token.cpp](../extras/data_token.cpp)

- 新增 `STREAMING_DMA_ALIGNMENT` 宏 (默认 64)
- `-DSTREAMING_DMA_ALIGNMENT=0` 可在无缓存 MCU 上关闭对齐
- `BufferPoolShard` 和 `::operator new` 均条件编译

---

## 三、零堆分配容器 (iceoryx 启发)

### FixedString\<N\>

**文件**: [include/mccc/protocol.hpp](../include/mccc/protocol.hpp)

- 栈上固定容量字符串，替代 `std::string`
- `TruncateToCapacity_t` 标记类型，强制调用方显式声明截断意图
- 编译期模板检查防止溢出: `static_assert(N - 1U <= Capacity)`

### FixedVector\<T, N\>

**文件**: [include/mccc/protocol.hpp](../include/mccc/protocol.hpp)

- 栈上固定容量向量，替代 `std::vector`
- `push_back` / `emplace_back` 返回 `bool`，无异常
- `erase_unordered` O(1) 删除 (swap with last)
- 用于 `Component<PayloadVariant>::handles_` 订阅句柄管理

### 应用点

| 替换位置 | 之前 | 之后 |
|---------|------|------|
| `CameraFrame::format` | `std::string` | `FixedString<16>` |
| `SystemLog::content` | `std::string` | `FixedString<64>` |
| `Component::handles_` | `std::vector<SubscriptionHandle>` | `FixedVector<SubscriptionHandle, 16>` |

---

## 四、协议层修复

### MessagePriority 注释修正

**文件**: [include/mccc/protocol.hpp](../include/mccc/protocol.hpp)

- **之前**: `HIGH` 注释为 "Always accepted, can jump queue" -- 与实现不符
- **现在**: 准确反映阈值准入机制

```cpp
enum class MessagePriority : uint8_t {
    LOW = 0U,    /** Dropped when queue >= 60% full */
    MEDIUM = 1U, /** Dropped when queue >= 80% full */
    HIGH = 2U    /** Dropped when queue >= 99% full (highest admission threshold) */
};
```

---

## 五、代码扫描修复

### 5.1 ReleaseCallback 函数指针 noexcept

**文件**: [extras/data_token.hpp](../extras/data_token.hpp)

- C++17 中 `noexcept` 是函数类型的一部分
- `ReleaseCallback` 改为 `void (*)(void*, uint32_t) noexcept`
- `~DataToken()` 标记 `noexcept`

### 5.2 日志格式跨平台兼容

**文件**: [extras/log_macro.hpp](../extras/log_macro.hpp)

- `%lu` 改为 `PRIu64` (来自 `<cinttypes>`)
- 32 位系统上 `uint64_t` 不一定是 `unsigned long`

---

## 六、变更文件汇总

| 文件 | 变更类型 | 关键改动 |
|------|---------|---------|
| `include/mccc/message_bus.hpp` | 重构 | OPT-1/2/3/4/7, R-1/R-2, 固定回调表 |
| `include/mccc/protocol.hpp` | 新增 | FixedString, FixedVector, 优先级注释修正 |
| `include/mccc/component.hpp` | 重构 | FixedVector handles_, SubscribeSafe noexcept |
| `extras/data_token.hpp` | 重写 | 函数指针替代虚基类, noexcept |
| `extras/buffer_pool.hpp` | 重构 | 移除 DMABufferReleaser, DMA 对齐可配置 |
| `extras/data_token.cpp` | 重写 | 函数指针释放, DMA 条件对齐 |
| `extras/log_macro.hpp` | 修复 | PRIu64 格式跨平台兼容 |

---

## 七、验证结果

| 验证项 | 结果 |
|-------|------|
| Release 编译 | 零错误零警告 |
| ASAN 编译 + 运行 | 零内存错误 |
| 功能测试 | 发送/处理/丢弃/错误 全部正确 |
| 热路径堆分配 | 零 (Publish + Borrow) |
