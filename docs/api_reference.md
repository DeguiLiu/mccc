# MCCC API 参考文档

> **MCCC** (Message-Centric Component Communication) — 面向安全关键嵌入式系统的 Lock-free MPSC 消息总线

## 目录

- [核心概念](#核心概念)
- [mccc.hpp — 核心定义](#mccchpp--核心定义)
  - [FixedString\<N\>](#fixedstringn)
  - [FixedVector\<T, N\>](#fixedvectort-n)
  - [MessagePriority](#messagepriority)
  - [MessageHeader](#messageheader)
  - [MessageEnvelope\<PayloadVariant\>](#messageenvelopepayloadvariant)
  - [make_overloaded](#make_overloaded)
  - [FixedFunction\<Sig, Capacity\>](#fixedfunctionsig-capacity)
- [mccc.hpp — 消息总线](#mccchpp--消息总线)
  - [AsyncBus\<PayloadVariant\>](#asyncbuspayloadvariant)
  - [发布 API](#发布-api)
  - [订阅 API](#订阅-api)
  - [处理 API](#处理-api)
  - [队列状态 API](#队列状态-api)
  - [性能模式](#性能模式)
  - [统计信息](#统计信息)
  - [错误处理](#错误处理)
- [component.hpp — 组件基类](#componenthpp--组件基类)
  - [Component\<PayloadVariant\>](#componentpayloadvariant)
- [static_component.hpp — CRTP 零开销组件](#static_componenthpp--crtp-零开销组件)
- [编译期配置宏](#编译期配置宏)
- [完整示例](#完整示例)

---

## 核心概念

MCCC 是一个 **Header-only** 的 C++17 消息总线库，核心设计：

| 概念 | 说明 |
|------|------|
| **PayloadVariant** | 用户定义的 `std::variant<...>` 消息类型集合 |
| **AsyncBus** | 单例消息总线，内部使用 Lock-free Ring Buffer |
| **MessageEnvelope** | 消息信封 = 消息头（ID、时间戳、优先级）+ 载荷 |
| **Component** | 可选基类，提供安全的订阅生命周期管理 |

**典型用法**:

```cpp
// 1. 定义消息类型
struct SensorData { float temperature; };
struct MotorCmd   { int32_t speed; };

// 2. 组合为 variant
using MyPayload = std::variant<SensorData, MotorCmd>;
using MyBus = mccc::AsyncBus<MyPayload>;

// 3. 订阅
MyBus::Instance().Subscribe<SensorData>([](const auto& env) {
    const auto* data = std::get_if<SensorData>(&env.payload);
    if (data) { /* 处理 */ }
});

// 4. 发布
MyBus::Instance().Publish(SensorData{25.5f}, /*sender_id=*/1);

// 5. 消费（需要显式调用）
MyBus::Instance().ProcessBatch();
```

---

## mccc.hpp — 核心定义

### FixedString\<N\>

栈上固定容量字符串，零堆分配。灵感来源于 iceoryx `iox::string<N>`。

```cpp
template <uint32_t Capacity>
class FixedString;
```

**模板参数**:
- `Capacity` — 最大字符数（不含 null 终止符），必须 > 0

#### 构造函数

| 签名 | 说明 |
|------|------|
| `FixedString()` | 默认构造，空字符串 |
| `FixedString(const char (&str)[N])` | 从字符串字面量构造（编译期检查长度） |
| `FixedString(TruncateToCapacity_t, const char* str)` | 从 C 字符串构造，超长截断 |
| `FixedString(TruncateToCapacity_t, const char* str, uint32_t count)` | 指定长度构造，超长截断 |
| `FixedString(TruncateToCapacity_t, const std::string& str)` | 从 std::string 构造，超长截断 |

**截断标记**:

```cpp
// TruncateToCapacity 强制调用者显式承认可能的数据丢失
mccc::FixedString<8> name(mccc::TruncateToCapacity, "very long string");
// name == "very lon"（被截断到 8 字符）
```

#### 成员函数

| 方法 | 返回类型 | 说明 |
|------|---------|------|
| `c_str()` | `const char*` | 返回 null 终止的 C 字符串 |
| `size()` | `uint32_t` | 当前字符串长度 |
| `capacity()` | `uint32_t` | 最大容量（静态） |
| `empty()` | `bool` | 是否为空 |
| `clear()` | `void` | 清空字符串 |
| `assign(TruncateToCapacity_t, const char*)` | `FixedString&` | 截断赋值 |
| `operator==(const FixedString<M>&)` | `bool` | 比较相等 |
| `operator!=(const FixedString<M>&)` | `bool` | 比较不等 |
| `operator==(const char (&)[N])` | `bool` | 与字符串字面量比较 |

---

### FixedVector\<T, N\>

栈上固定容量容器，零堆分配。

```cpp
template <typename T, uint32_t Capacity>
class FixedVector;
```

**模板参数**:
- `T` — 元素类型
- `Capacity` — 最大元素数，必须 > 0

#### 成员类型

| 类型 | 定义 |
|------|------|
| `value_type` | `T` |
| `size_type` | `uint32_t` |
| `iterator` | `T*` |
| `const_iterator` | `const T*` |

#### 构造 / 析构

| 签名 | 说明 |
|------|------|
| `FixedVector()` | 默认构造，空容器 |
| `~FixedVector()` | 析构，销毁所有元素 |
| `FixedVector(const FixedVector&)` | 拷贝构造 |
| `FixedVector(FixedVector&&)` | 移动构造 |

#### 容量

| 方法 | 返回类型 | 说明 |
|------|---------|------|
| `empty()` | `bool` | 是否为空 |
| `size()` | `uint32_t` | 当前元素数 |
| `capacity()` | `uint32_t` | 最大容量（静态） |
| `full()` | `bool` | 是否已满 |

#### 修改器

| 方法 | 返回类型 | 说明 |
|------|---------|------|
| `push_back(const T&)` | `bool` | 拷贝添加，满时返回 false |
| `push_back(T&&)` | `bool` | 移动添加，满时返回 false |
| `emplace_back(Args&&...)` | `bool` | 原地构造，满时返回 false |
| `pop_back()` | `bool` | 移除末尾元素，空时返回 false |
| `erase_unordered(uint32_t index)` | `bool` | 无序删除（用最后元素填充），越界返回 false |
| `clear()` | `void` | 清空所有元素 |

#### 元素访问

| 方法 | 说明 |
|------|------|
| `operator[](uint32_t)` | 无边界检查访问 |
| `front()` / `back()` | 首/尾元素 |
| `data()` | 底层数组指针 |
| `begin()` / `end()` | 迭代器 |

---

### MessagePriority

消息优先级，用于背压准入控制。

```cpp
enum class MessagePriority : uint8_t {
    LOW    = 0U,   // 队列 >= 60% 满时拒绝
    MEDIUM = 1U,   // 队列 >= 80% 满时拒绝
    HIGH   = 2U    // 队列 >= 99% 满时拒绝
};
```

**准入阈值**:

```
队列深度 ─────────────────────────────────────────────────────→ 100%
          │                    │                │         │
          0%                  60%              80%       99%
          │← LOW/MED/HIGH →│← MED/HIGH →│← HIGH →│← 全拒 →│
```

---

### MessageHeader

消息头，用于追踪和调试。

```cpp
struct MessageHeader {
    uint64_t        msg_id;        // 全局递增 ID
    uint64_t        timestamp_us;  // 微秒时间戳 (steady_clock)
    uint32_t        sender_id;     // 发送者标识
    MessagePriority priority;      // 消息优先级
};
```

---

### MessageEnvelope\<PayloadVariant\>

消息信封，值类型，直接内嵌在 Ring Buffer 中。

```cpp
template <typename PayloadVariant>
struct MessageEnvelope {
    MessageHeader  header;
    PayloadVariant payload;
};
```

---

### make_overloaded

C++14 兼容的 `std::visit` 辅助工具（替代 C++17 的类模板推导指南）。

```cpp
template <class... Ts>
overloaded<Ts...> make_overloaded(Ts... ts);
```

**用法**:

```cpp
using MyPayload = std::variant<SensorData, MotorCmd>;
MyPayload msg = SensorData{25.0f};

std::visit(mccc::make_overloaded(
    [](const SensorData& s) { /* 处理传感器数据 */ },
    [](const MotorCmd& m)   { /* 处理电机命令 */ }
), msg);
```

---

### FixedFunction\<Sig, Capacity\>

栈上固定容量类型擦除 callable，替代 `std::function`。零堆分配，`static_assert` 超容量编译失败。

```cpp
template <typename Signature, uint32_t Capacity = 48U>
class FixedFunction;
```

**模板参数**:
- `Signature` — 函数签名，如 `void(int)`, `int(float, float)`
- `Capacity` — 内联存储字节数（默认 48），callable 超过此大小编译失败

#### 构造函数

| 签名 | 说明 |
|------|------|
| `FixedFunction()` | 默认构造，空状态 |
| `FixedFunction(nullptr_t)` | 空状态 |
| `FixedFunction(F&& f)` | 从 callable 构造（`static_assert(sizeof(F) <= Capacity)`） |
| `FixedFunction(FixedFunction&&)` | 移动构造 |

#### 成员函数

| 方法 | 说明 |
|------|------|
| `operator bool()` | 是否持有 callable |
| `operator()(Args...)` | 调用（空时返回 `R{}`） |
| `operator=(FixedFunction&&)` | 移动赋值 |
| `operator=(nullptr_t)` | 清空 |

**与 std::function 对比**:

| 特性 | `std::function` | `FixedFunction<Sig, 48>` |
|------|:---:|:---:|
| 堆分配 | 可能 (>16B) | **永不** |
| 超容量行为 | 运行时 malloc | **编译期报错** |
| 异常路径 | 有 | **无** |

---

## mccc.hpp — 消息总线

### AsyncBus\<PayloadVariant\>

Lock-free MPSC 消息总线，单例模式。

```cpp
template <typename PayloadVariant>
class AsyncBus;
```

**模板参数**:
- `PayloadVariant` — `std::variant<...>`，用户定义的消息类型集合

#### 获取实例

```cpp
static AsyncBus& Instance() noexcept;
```

每个 `PayloadVariant` 类型有独立的单例实例。

---

### 发布 API

#### Publish

默认优先级（MEDIUM）发布消息。

```cpp
bool Publish(PayloadVariant&& payload, uint32_t sender_id) noexcept;
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `payload` | `PayloadVariant&&` | 消息载荷（右值引用，零拷贝入队） |
| `sender_id` | `uint32_t` | 发送者标识 |

**返回值**: `true` 入队成功，`false` 队列满或被准入控制拒绝

#### PublishWithPriority

指定优先级发布消息。

```cpp
bool PublishWithPriority(PayloadVariant&& payload, uint32_t sender_id,
                         MessagePriority priority) noexcept;
```

#### PublishFast

使用外部提供的时间戳发布（避免 `steady_clock::now()` 调用开销）。

```cpp
bool PublishFast(PayloadVariant&& payload, uint32_t sender_id,
                 uint64_t timestamp_us) noexcept;
```

---

### 订阅 API

#### Subscribe

注册类型化的消息回调。

```cpp
template <typename T, typename Func>
SubscriptionHandle Subscribe(Func&& func);
```

| 参数 | 说明 |
|------|------|
| `T` | 要订阅的消息类型（必须是 `PayloadVariant` 的成员类型） |
| `func` | 回调函数，签名 `void(const MessageEnvelope<PayloadVariant>&)` |

**返回值**: `SubscriptionHandle`，用于后续取消订阅。如果回调槽位已满，返回无效 handle（`callback_id == -1`）。

**注意**: 回调在 `ProcessBatch()` 的调用线程中执行。

```cpp
auto handle = bus.Subscribe<SensorData>([](const auto& env) {
    const auto* data = std::get_if<SensorData>(&env.payload);
    if (data) {
        printf("温度: %.1f\n", data->temperature);
    }
});
```

#### Unsubscribe

取消订阅。

```cpp
bool Unsubscribe(const SubscriptionHandle& handle) noexcept;
```

**返回值**: `true` 成功取消，`false` handle 无效或已被取消

---

### 处理 API

#### ProcessBatch

从 Ring Buffer 中消费并分发消息（单消费者调用）。

```cpp
uint32_t ProcessBatch() noexcept;
```

**返回值**: 本次处理的消息数（最多 `BATCH_PROCESS_SIZE = 1024` 条）

**使用模式**:

```cpp
// 方式 1: 专用消费者线程
std::thread consumer([&bus, &running]() {
    while (running) {
        bus.ProcessBatch();
    }
});

// 方式 2: 主循环轮询
while (true) {
    bus.ProcessBatch();
    // ... 其他工作 ...
}
```

#### ProcessBatchWith

零开销编译期分发。绕过回调表和 `shared_mutex`，使用 `std::visit` 直接分发。

```cpp
template <typename Visitor>
uint32_t ProcessBatchWith(Visitor&& vis) noexcept;
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `vis` | `Visitor&&` | 可调用对象，必须处理 `PayloadVariant` 中所有类型 |

**返回值**: 本次处理的消息数

**与 ProcessBatch 对比**:

| 操作 | ProcessBatch | ProcessBatchWith |
|------|:---:|:---:|
| `shared_mutex` 读锁 | 有 | **无** |
| 回调表遍历 | 有 | **无** |
| FixedFunction 间接调用 | 有 | **无** |
| 可内联 | 否 | **是** |

**使用方式**:

```cpp
auto visitor = mccc::make_overloaded(
    [](const SensorData& d) { process(d); },
    [](const MotorCmd& c) { execute(c); }
);
bus.ProcessBatchWith(visitor);
```

---

### 队列状态 API

#### QueueDepth

当前队列中未处理的消息数。

```cpp
uint32_t QueueDepth() const noexcept;
```

#### QueueUtilizationPercent

队列利用率百分比 (0-100)。

```cpp
uint32_t QueueUtilizationPercent() const noexcept;
```

#### GetBackpressureLevel

获取当前背压级别。

```cpp
BackpressureLevel GetBackpressureLevel() const noexcept;
```

```cpp
enum class BackpressureLevel : uint8_t {
    NORMAL   = 0U,   // < 75% 满
    WARNING  = 1U,   // 75-90% 满
    CRITICAL = 2U,   // 90-100% 满
    FULL     = 3U    // 100% 满
};
```

---

### 性能模式

```cpp
enum class PerformanceMode : uint8_t {
    FULL_FEATURED = 0U,   // 完整功能：优先级准入 + 统计 + 错误回调
    BARE_METAL    = 1U,   // 裸机模式：跳过优先级检查、统计、错误回调
    NO_STATS      = 2U    // 无统计：保留优先级准入，跳过统计计数
};
```

#### SetPerformanceMode

```cpp
void SetPerformanceMode(PerformanceMode mode) noexcept;
```

**性能对比**:

| 模式 | 吞吐量 | 功能 |
|------|--------|------|
| BARE_METAL | ~18 M/s | 仅入队出队 |
| NO_STATS | ~8 M/s | 优先级准入 |
| FULL_FEATURED | ~5 M/s | 全部功能 |

---

### 统计信息

#### GetStatistics

获取统计快照（无锁读取）。

```cpp
BusStatisticsSnapshot GetStatistics() const noexcept;
```

```cpp
struct BusStatisticsSnapshot {
    uint64_t messages_published;        // 成功发布的消息总数
    uint64_t messages_dropped;          // 被拒绝的消息总数
    uint64_t messages_processed;        // 已处理的消息总数
    uint64_t processing_errors;         // 处理错误次数
    uint64_t high_priority_published;   // HIGH 优先级发布数
    uint64_t medium_priority_published; // MEDIUM 优先级发布数
    uint64_t low_priority_published;    // LOW 优先级发布数
    uint64_t high_priority_dropped;     // HIGH 优先级丢弃数
    uint64_t medium_priority_dropped;   // MEDIUM 优先级丢弃数
    uint64_t low_priority_dropped;      // LOW 优先级丢弃数
};
```

#### ResetStatistics

重置所有统计计数器。

```cpp
void ResetStatistics() noexcept;
```

---

### 错误处理

#### SetErrorCallback

设置错误回调（函数指针，不是 `std::function`，零开销）。

```cpp
void SetErrorCallback(ErrorCallback callback) noexcept;

// ErrorCallback 类型
using ErrorCallback = void (*)(BusError, uint64_t);
```

```cpp
enum class BusError : uint8_t {
    QUEUE_FULL        = 0U,   // 队列满，消息被丢弃
    INVALID_MESSAGE   = 1U,   // 无效消息
    PROCESSING_ERROR  = 2U,   // 处理回调异常
    OVERFLOW_DETECTED = 3U    // 消息 ID 即将溢出
};
```

**用法**:

```cpp
bus.SetErrorCallback([](mccc::BusError err, uint64_t msg_id) {
    if (err == mccc::BusError::QUEUE_FULL) {
        LOG_WARN("消息 %lu 被丢弃：队列满", msg_id);
    }
});

// 清除回调
bus.SetErrorCallback(nullptr);
```

---

## component.hpp — 组件基类

### Component\<PayloadVariant\>

可选的组件基类，提供安全的订阅生命周期管理。

```cpp
template <typename PayloadVariant>
class Component : public std::enable_shared_from_this<Component<PayloadVariant>>;
```

**核心特性**:
- 析构时自动取消所有订阅（RAII）
- 使用 `weak_ptr` 防止回调中访问已销毁对象
- 使用 `FixedVector` 管理订阅句柄，零堆分配

#### SubscribeSafe

安全订阅，回调接收 `shared_ptr<Component>` 作为 self 参数。

```cpp
template <typename T, typename Func>
void SubscribeSafe(Func&& callback) noexcept;
```

**回调签名**: `void(shared_ptr<Component>, const T&, const MessageHeader&)`

```cpp
class MyComponent : public mccc::Component<MyPayload> {
public:
    static std::shared_ptr<MyComponent> create() {
        auto ptr = std::shared_ptr<MyComponent>(new MyComponent());
        ptr->InitializeComponent();
        ptr->SubscribeSafe<SensorData>(
            [](std::shared_ptr<Component> self_base,
               const SensorData& data,
               const mccc::MessageHeader& hdr) {
                auto self = std::static_pointer_cast<MyComponent>(self_base);
                self->OnSensorData(data);
            });
        return ptr;
    }

private:
    MyComponent() = default;
    void OnSensorData(const SensorData& data) { /* ... */ }
};
```

#### SubscribeSimple

简单订阅，回调不接收 self 指针。

```cpp
template <typename T, typename Func>
void SubscribeSimple(Func&& callback) noexcept;
```

**回调签名**: `void(const T&, const MessageHeader&)`

#### InitializeComponent

组件初始化（当前为空操作，可扩展）。

```cpp
void InitializeComponent() noexcept;
```

**注意**: `Component` 必须通过 `std::shared_ptr` 持有（因为继承了 `enable_shared_from_this`），不能在栈上或裸 `new` 构造。

---

## static_component.hpp — CRTP 零开销组件

### StaticComponent\<Derived, PayloadVariant\>

CRTP 零开销组件基类，Handler 在编译期静态分发。

```cpp
template <typename Derived, typename PayloadVariant>
class StaticComponent;
```

**与 Component 对比**:

| 特性 | Component | StaticComponent |
|------|:---:|:---:|
| 虚析构函数 | 有 | **无** |
| shared_ptr / weak_ptr | 有 | **无** |
| 运行时订阅/退订 | 有 | 无 |
| Handler 可内联 | 否 | **是** |
| 适用场景 | 动态订阅 | 编译期确定的处理 |

#### MakeVisitor

创建可传给 `ProcessBatchWith` 的 visitor。

```cpp
auto MakeVisitor() noexcept;
```

#### HasHandler\<Derived, T\>

SFINAE trait，编译期检测 Derived 是否有 `Handle(const T&)` 方法。

```cpp
template <typename Derived, typename T>
struct HasHandler;  // ::value = true/false
```

**使用示例**:

```cpp
class MySensor : public mccc::StaticComponent<MySensor, MyPayload> {
 public:
  void Handle(const SensorData& d) noexcept { process(d); }
  void Handle(const MotorCmd& c) noexcept { execute(c); }
  // LogMsg 未处理 -> 编译期忽略
};

MySensor sensor;
auto visitor = sensor.MakeVisitor();
bus.ProcessBatchWith(visitor);
```

---

## 编译期配置宏

在 `#include <mccc/mccc.hpp>` **之前**定义这些宏来自定义配置：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `MCCC_QUEUE_DEPTH` | 131072 (128K) | Ring Buffer 深度，**必须是 2 的幂** |
| `MCCC_CACHELINE_SIZE` | 64 | 缓存行大小（字节） |
| `MCCC_SINGLE_PRODUCER` | 0 | SPSC wait-free 快速路径 (1 = 跳过 CAS) |
| `MCCC_SINGLE_CORE` | 0 | 单核模式 (1 = 关闭缓存行对齐 + relaxed + signal_fence) |
| `MCCC_MAX_MESSAGE_TYPES` | 8 | variant 中最大消息类型数 |
| `MCCC_MAX_CALLBACKS_PER_TYPE` | 16 | 每种消息类型的最大回调数 |
| `MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT` | 16 | 每个组件的最大订阅数 |

**示例**:

```cpp
#define MCCC_QUEUE_DEPTH 65536U           // 64K 队列
#define MCCC_MAX_MESSAGE_TYPES 4U         // 仅 4 种消息类型
#define MCCC_MAX_CALLBACKS_PER_TYPE 8U    // 每类最多 8 个回调
#include <mccc/mccc.hpp>
```

---

## 完整示例

### 最小可运行示例

```cpp
#include <mccc/mccc.hpp>
#include <cstdio>

struct Temperature { float celsius; };
struct Humidity    { float percent; };

using Payload = std::variant<Temperature, Humidity>;
using Bus = mccc::AsyncBus<Payload>;

int main() {
    auto& bus = Bus::Instance();

    // 订阅
    bus.Subscribe<Temperature>([](const auto& env) {
        const auto* t = std::get_if<Temperature>(&env.payload);
        if (t) printf("温度: %.1f°C\n", t->celsius);
    });

    bus.Subscribe<Humidity>([](const auto& env) {
        const auto* h = std::get_if<Humidity>(&env.payload);
        if (h) printf("湿度: %.1f%%\n", h->percent);
    });

    // 发布
    bus.Publish(Temperature{25.5f}, /*sender_id=*/1);
    bus.Publish(Humidity{60.0f}, /*sender_id=*/2);

    // 消费
    bus.ProcessBatch();

    return 0;
}
```

### 多线程生产者-消费者

```cpp
#include <mccc/mccc.hpp>
#include <atomic>
#include <thread>

struct SensorReading { uint32_t sensor_id; float value; };
using Payload = std::variant<SensorReading>;
using Bus = mccc::AsyncBus<Payload>;

int main() {
    auto& bus = Bus::Instance();
    bus.SetPerformanceMode(Bus::PerformanceMode::FULL_FEATURED);

    std::atomic<uint64_t> processed{0};

    bus.Subscribe<SensorReading>([&processed](const auto& env) {
        processed.fetch_add(1, std::memory_order_relaxed);
    });

    // 消费者线程
    std::atomic<bool> running{true};
    std::thread consumer([&]() {
        while (running.load(std::memory_order_acquire)) {
            bus.ProcessBatch();
        }
        while (bus.ProcessBatch() > 0) {}  // 排空
    });

    // 多个生产者
    std::vector<std::thread> producers;
    for (uint32_t id = 0; id < 4; ++id) {
        producers.emplace_back([&bus, id]() {
            for (int i = 0; i < 10000; ++i) {
                bus.Publish(SensorReading{id, static_cast<float>(i)}, id);
            }
        });
    }

    for (auto& p : producers) p.join();

    // 等待消费完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running.store(false, std::memory_order_release);
    consumer.join();

    printf("处理了 %lu 条消息\n", processed.load());
    return 0;
}
```

### 背压监控

```cpp
auto bp = bus.GetBackpressureLevel();
switch (bp) {
    case mccc::BackpressureLevel::NORMAL:
        break;  // 正常
    case mccc::BackpressureLevel::WARNING:
        LOG_WARN("队列 75%%+ 满，考虑降低发布速率");
        break;
    case mccc::BackpressureLevel::CRITICAL:
        LOG_ERROR("队列 90%%+ 满，LOW 消息正在被丢弃");
        break;
    case mccc::BackpressureLevel::FULL:
        LOG_ERROR("队列已满，所有消息被丢弃");
        break;
}
```

---

## SubscriptionHandle

```cpp
struct SubscriptionHandle {
    size_t type_index;   // 消息类型索引
    size_t callback_id;  // 回调 ID
};
```

调用 `Subscribe` 返回，传给 `Unsubscribe` 取消订阅。无效 handle 的 `callback_id == static_cast<size_t>(-1)`。
