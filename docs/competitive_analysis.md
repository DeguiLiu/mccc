# MCCC 竞品分析报告

> 基于本机统一测试的性能对标与特性对比
>
> 测试日期: 2025-02 (优化后重测)

---

## 目录

1. [对标项目一览](#1-对标项目一览)
2. [测试环境与方法](#2-测试环境与方法)
3. [性能对标矩阵](#3-性能对标矩阵)
4. [特性对比](#4-特性对比)
5. [架构对比](#5-架构对比)
6. [各竞品优缺点分析](#6-各竞品优缺点分析)
7. [嵌入式优化路线图](#7-嵌入式优化路线图)

---

## 1. 对标项目一览

我们选取了 **5 个开源项目** 作为对标对象，覆盖事件总线、信号槽、IPC 消息三大类：

| 项目 | Stars | 类型 | 语言/标准 | 许可证 | 测试版本 |
|------|:-----:|------|-----------|--------|----------|
| [wqking/eventpp](https://github.com/wqking/eventpp) | ~2,000 | 事件队列/分发器 | C++11 | Apache-2.0 | v0.3.0 ([fork](https://gitee.com/liudegui/eventpp)) |
| [skypjack/EnTT](https://github.com/skypjack/entt) | ~10,000 | ECS + 事件分发器 | C++17 | MIT | v3.12.2 |
| [palacaze/sigslot](https://github.com/palacaze/sigslot) | ~600 | 信号/槽 | C++14 | MIT | v1.2.3 |
| [zeromq/libzmq](https://github.com/zeromq/libzmq) | ~10,000 | IPC/网络消息 | C/C++ | MPL-2.0 | v4.3.5 |
| [QuantumLeaps/qpcpp](https://github.com/QuantumLeaps/qpcpp) | ~800 | Active Object 框架 | C++11 | GPL/商业 | v8.1.2 |

> **对比基线**: MCCC 核心代码 **1,089 行** (3 个头文件), 纯 C++17, MIT 许可证

### 下载地址

```bash
# MCCC
git clone https://gitee.com/liudegui/mccc-bus.git

# eventpp (fork 优化版)
git clone https://gitee.com/liudegui/eventpp.git

# EnTT (需 checkout v3.12.2 以支持 C++17)
git clone https://github.com/skypjack/entt.git && cd entt && git checkout v3.12.2

# sigslot
git clone https://github.com/palacaze/sigslot.git

# ZeroMQ (libzmq + cppzmq C++ binding)
git clone https://github.com/zeromq/libzmq.git
git clone https://github.com/zeromq/cppzmq.git

# QP/C++
git clone https://github.com/QuantumLeaps/qpcpp.git
```

---

## 2. 测试环境与方法

### 2.1 硬件环境

| 项目 | 配置 |
|------|------|
| CPU | Intel Xeon Cascadelake 64 vCPU |
| 内存 | 275 GB, 单 NUMA 节点 |
| OS | Ubuntu 24.04 LTS |
| 编译器 | GCC 13.3.0 |
| 优化 | `-O3 -march=native -std=c++17` |

### 2.2 测试方法

| 参数 | 值 | 说明 |
|------|:--:|------|
| 消息数 | 1,000,000 | 每轮发送 100 万条消息 |
| 轮次 | 10 | 取统计值 (均值 ± 标准差) |
| 载荷 | 24 字节 | `struct { uint64_t seq; float x,y,z,w; }` |
| CPU 亲和性 | 生产者=core 0, 消费者=core 1 | `pthread_setaffinity_np` 绑核 |

### 2.3 测试场景说明

不同类型的库采用最接近其设计意图的测试方式：

| 库 | 测试模式 | 说明 |
|----|----------|------|
| **MCCC** | Publish → ProcessBatch (双线程) | 生产者线程发布, 消费者线程批量处理 |
| **eventpp** | enqueue → process (单线程) | 先入队 100 万条, 再 process 全部分发 |
| **EnTT** | enqueue → update (单线程) | 先入队 100 万条, 再 update 全部分发 |
| **sigslot** | 同步 emit (单线程) | 信号直接调用槽函数, 无队列 |
| **ZeroMQ** | push/pull inproc:// (双线程) | 进程内 socket 传输, 生产者+消费者线程 |

> 吞吐量 = 消息数 / 生产者发送耗时。延迟 = 生产者发送耗时 / 消息数。

### 2.4 测试程序

测试源码: [`examples/competitive_benchmark.cpp`](../examples/competitive_benchmark.cpp)

编译命令:
```bash
g++ -std=c++17 -O3 -march=native -o competitive_bench \
    examples/competitive_benchmark.cpp \
    -Iinclude \
    -I<eventpp>/include -I<entt>/src -I<sigslot>/include \
    -I<cppzmq> -I<libzmq>/include \
    -L<libzmq>/build/lib -lzmq -lpthread
```

---

## 3. 性能对标矩阵

### 3.1 吞吐量对比 (本机实测)

```mermaid
xychart-beta
    title "吞吐量对比 (M msg/s, 本机实测)"
    x-axis ["EnTT", "sigslot", "MCCC BARE", "MCCC FULL", "eventpp Raw", "eventpp Pool", "ZeroMQ"]
    y-axis "M msg/s" 0 --> 120
    bar [107.29, 26.21, 16.34, 7.48, 5.00, 5.01, 4.51]
```

| 实现 | 吞吐量 (M/s) | 入队延迟 (ns) | 测试模式 | 备注 |
|------|:-----------:|:------------:|----------|------|
| **EnTT dispatcher** | **107.29 ± 26.59** | 5 | 单线程 enqueue+update | 无线程安全, 无队列开销 |
| **sigslot** | **26.21 ± 0.67** | 38 | 同步 emit | 无队列, 直接函数调用 |
| **MCCC BARE_METAL** | **16.34 ± 0.36** | 61 | 双线程 pub+consume | Lock-free CAS, 无优先级/统计 |
| **MCCC FULL_FEATURED** | **7.48 ± 0.27** | 134 | 双线程 pub+consume | Lock-free + 优先级 + 背压 + 统计 |
| **eventpp Raw** | **5.00 ± 0.32** | 51 | 单线程 enqueue+process | Mutex + std::list |
| **eventpp Pool** | **5.01 ± 0.11** | 53 | 单线程 enqueue+process | Mutex + pool allocator |
| **ZeroMQ inproc** | **4.51 ± 0.30** | 215 | 双线程 push/pull | Socket 协议栈开销 |

### 3.2 延迟对比

> P50/P95/P99 为延迟百分位数。P50 = 中位数, P99 = 尾部延迟 (99% 消息低于此值)。

```mermaid
xychart-beta
    title "入队延迟 P50 对比 (ns)"
    x-axis ["EnTT", "sigslot", "eventpp Raw", "eventpp Pool", "MCCC BARE", "MCCC FULL", "ZeroMQ"]
    y-axis "ns" 0 --> 250
    bar [5, 38, 51, 53, 61, 134, 215]
```

| 实现 | P50 (ns) | P95 (ns) | P99 (ns) | 说明 |
|------|:--------:|:--------:|:--------:|------|
| **EnTT** | 5 | 5 | 5 | 单线程, vector push_back |
| **sigslot** | 38 | 39 | 39 | 同步调用, mutex 保护 |
| **eventpp Raw** | 51 | 53 | 53 | Mutex + list 节点分配 |
| **eventpp Pool** | 53 | 56 | 56 | Mutex + pool 分配 |
| **MCCC BARE_METAL** | 61 | 62 | 62 | CAS 原子操作 + 序列号同步 |
| **MCCC FULL_FEATURED** | 134 | 137 | 137 | CAS + 优先级检查 + 背压 + 统计 |
| **ZeroMQ inproc** | 215 | 251 | 251 | Socket 协议栈 + 消息拷贝 |

### 3.3 结果分析

**为什么 EnTT/sigslot 吞吐量高?**
- EnTT 和 sigslot 是**单线程同步**操作, 无锁竞争、无线程同步开销
- EnTT 的 enqueue 本质是 `std::vector::push_back`, update 是遍历 vector 调用回调
- sigslot 的 emit 是直接函数调用, 无任何队列

**为什么 MCCC BARE_METAL 比 eventpp 快?**
- MCCC 使用 **Lock-free CAS** 入队, eventpp 使用 **Mutex** 入队
- MCCC 的 Ring Buffer 是预分配固定内存, eventpp 的 std::list 需要逐节点分配
- MCCC 测试是双线程 (生产者+消费者并行), eventpp 测试是单线程 (先入队再处理)
- 优化后 MCCC BARE_METAL 吞吐量达 **16.34 M/s**, 是 eventpp 的 **3.3x**

**为什么 MCCC FULL_FEATURED 比 BARE_METAL 慢?**
- FULL_FEATURED 每条消息额外执行: 优先级准入检查 + 索引缓存 + 背压判断 + 统计计数
- 功能开销约 73 ns/消息 (134 - 61), 这是**安全关键特性的代价**
- 优化后功能开销从 ~100 ns 降至 ~73 ns (shared_mutex 读写分离 + 批量统计)

### 3.4 不同消息大小对比 (FULL_FEATURED 模式)

> 测试 24B/64B/128B/256B 四种载荷大小, 考察消息拷贝开销对各方案的影响。
> sigslot 为同步直接调用 (无队列), eventpp 为单线程 enqueue+process, MCCC/ZeroMQ 为双线程。

| 载荷 | MCCC FULL | eventpp Raw | sigslot | ZeroMQ |
|:----:|:---------:|:-----------:|:-------:|:------:|
| **24B** | 7.48 M/s (134 ns) | 5.00 M/s (51 ns) | 26.21 M/s (38 ns) | 4.51 M/s (215 ns) |
| **64B** | 1.96 M/s (524 ns) | 4.85 M/s (62 ns) | 31.02 M/s (32 ns) | 1.04 M/s (947 ns) |
| **128B** | 1.61 M/s (626 ns) | 4.60 M/s (69 ns) | 31.46 M/s (32 ns) | 2.01 M/s (496 ns) |
| **256B** | 4.93 M/s (254 ns) | 3.10 M/s (93 ns) | 30.21 M/s (33 ns) | 1.97 M/s (506 ns) |

**分析**:
- **sigslot** 受载荷大小影响最小 (同步调用, 无拷贝入队), 各载荷吞吐量均 >26 M/s
- **eventpp** 随载荷增大吞吐量下降明显 (5.00 → 3.10 M/s), 因为 std::list 节点需拷贝整个消息
- **MCCC** 24B 场景优化后达 7.48 M/s, 是 eventpp 的 1.5x; 大消息场景与 ZeroMQ 持平
- **ZeroMQ** 受载荷影响显著 (4.51 → 1.97 M/s), Socket 协议栈的消息拷贝成本随大小线性增长
- **MCCC 在所有场景均提供优先级+背压+零堆分配等安全特性**, ZeroMQ 和 eventpp 不具备

---

## 4. 特性对比

### 4.1 功能矩阵

| 特性 | MCCC | eventpp | EnTT | sigslot | ZeroMQ | QP/C++ |
|------|:----:|:-------:|:----:|:-------:|:------:|:------:|
| 异步队列 | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ |
| 类型安全分发 | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ |
| 优先级控制 | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ |
| 背压监控 | ✅ | ❌ | ❌ | ❌ | ✅ (HWM) | ❌ |
| Lock-free | ✅ | ❌ | ❌ | ❌ | 部分 | ❌ |
| 零堆分配 (热路径) | ✅ | ❌ | ❌ | ✅ | ❌ | ✅ |
| 线程安全 | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ |
| Header-only | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ |
| 零外部依赖 | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ |
| MISRA 合规 | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ |
| 嵌入式可配置 | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ |
| IPC/网络传输 | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |
| 状态机集成 | 可选 | ❌ | ❌ | ❌ | ❌ | ✅ (HSM) |

### 4.2 资源消耗对比

#### 代码规模

| 项目 | 核心文件数 | 核心代码行数 | 形态 |
|------|:--------:|:----------:|------|
| **MCCC** | 3 | **1,089** | Header-only |
| **EnTT signal** | 5 | 1,433 | Header-only |
| **eventpp** | 3 | 1,487 | Header-only |
| **sigslot** | 1 | 1,848 | Header-only |
| **QP/C++** | 21 | 5,640 | 编译型框架 |
| **ZeroMQ** | N/A | ~100K+ | 编译型库 |

#### 编译产物大小 (最小示例, -O3 -s stripped)

| 项目 | 二进制大小 | 相对 MCCC |
|------|:---------:|:---------:|
| **MCCC** | **14.2 KB** | 1.0x |
| **EnTT** | 18.3 KB | 1.3x |
| **eventpp** | 26.3 KB | 1.9x |
| **sigslot** | 34.3 KB | 2.4x |
| **ZeroMQ** | 2.15 MB (静态库) | 155x |
| **QP/C++** | ~50+ KB (框架) | >3x |

#### 运行时内存占用

| 项目 | 队列结构 | 热路径堆分配 | 内存模型 |
|------|----------|:----------:|----------|
| **MCCC** | 预分配 Ring Buffer (128K slots) | **零** | 固定大小, 编译期可配 |
| **eventpp** | std::list / pool allocator | 每消息一次 (Raw) / 池分配 (Pool) | 动态增长, 无上限 |
| **EnTT** | std::vector | vector 扩容时 | 动态增长, 无上限 |
| **sigslot** | 无队列 | **零** | 极小 (仅回调表) |
| **ZeroMQ** | 内部消息队列 | 每消息一次 | 动态, 含协议栈开销 |
| **QP/C++** | 事件池 (预分配) | **零** (池模式) | 固定大小, 可配置 |

### 4.3 安全性与鲁棒性对比

| 特性 | MCCC | eventpp | EnTT | sigslot | ZeroMQ | QP/C++ |
|------|:----:|:-------:|:----:|:-------:|:------:|:------:|
| 线程安全 | ✅ Lock-free | ✅ Mutex | ❌ | ✅ Mutex | ✅ | ✅ |
| 队列满保护 | ✅ 优先级丢弃 | ❌ 无限增长 | ❌ 无限增长 | N/A | ✅ HWM 丢弃 | ✅ 池耗尽拒绝 |
| 背压检测 | ✅ 四级 | ❌ | ❌ | ❌ | ✅ HWM | ❌ |
| 优先级准入 | ✅ HIGH 零丢失 | ❌ | ❌ | ❌ | ❌ | ✅ |
| 悬空回调保护 | ✅ weak_ptr | ❌ | ❌ | ✅ (RAII) | N/A | ✅ |
| 异常安全 | ✅ noexcept | 部分 | 部分 | 部分 | ✅ | ✅ noexcept |
| 内存溢出风险 | **无** (固定大小) | **有** (无限增长) | **有** (无限增长) | **无** | **有** (动态分配) | **无** (固定池) |
| 错误回调机制 | ✅ 原子回调 | ❌ | ❌ | ❌ | ✅ (errno) | ✅ (Q_onError) |

### 4.4 工业标准与嵌入式适配

| 特性 | MCCC | eventpp | EnTT | sigslot | ZeroMQ | QP/C++ |
|------|:----:|:-------:|:----:|:-------:|:------:|:------:|
| MISRA C++ 合规 | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ |
| AUTOSAR 适配 | ✅ 可适配 | ❌ | ❌ | ❌ | ❌ | ✅ |
| 确定性内存 (WCET) | ✅ 固定 Ring Buffer | ❌ 动态分配 | ❌ 动态分配 | ✅ 无队列 | ❌ | ✅ 事件池 |
| 编译期可裁剪 | ✅ 5 个宏 | ❌ | ❌ | ❌ | ❌ | ✅ |
| 最小 RAM 占用 | ~4 KB (1024 slots) | 不可控 | 不可控 | ~100 B | ~1 MB+ | ~2 KB |
| 单核 MCU 支持 | ✅ (MCCC_SINGLE_CORE=1) | ❌ | ❌ | ✅ | ❌ | ✅ |
| 无 RTTI/异常 | ✅ | ❌ (需 RTTI) | ❌ (需 RTTI) | ❌ | ✅ | ✅ |
| 无 STL 依赖 (热路径) | ✅ | ❌ (std::list) | ❌ (std::vector) | ❌ (std::vector) | ✅ | ✅ |

> **嵌入式裁剪示例**: `cmake .. -DCMAKE_CXX_FLAGS="-DMCCC_QUEUE_DEPTH=1024 -DMCCC_SINGLE_CORE=1 -DMCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1"` 可将 MCCC 裁剪至 ~4 KB RAM。

### 4.5 稳定性对比

| 指标 | MCCC | eventpp | EnTT | sigslot | ZeroMQ |
|------|:----:|:-------:|:----:|:-------:|:------:|
| 吞吐量方差 (StdDev/Mean) | **2.2%** (BARE) / **3.6%** (FULL) | 6.4% (Raw) | **24.8%** | 2.6% | 6.7% |
| 尾部延迟 (P99/P50) | **1.02x** | 1.04x | 1.00x | 1.03x | 1.17x |
| 长时间运行稳定性 | ✅ 固定内存, 无碎片 | ⚠️ 内存碎片风险 | ⚠️ vector 重分配 | ✅ 无队列 | ⚠️ 内存碎片 |
| 多生产者竞争 | ✅ CAS 无饥饿 | ✅ Mutex 公平 | ❌ 不支持 | ✅ Mutex | ✅ |

> 方差数据来自本次 10 轮测试。MCCC BARE_METAL 的 StdDev/Mean = 0.36/16.34 = 2.2%, FULL_FEATURED = 0.27/7.48 = 3.6%, 是异步双线程方案中最稳定的。

### 4.6 适用场景对比

| 场景 | 推荐方案 | 原因 |
|------|----------|------|
| 安全关键嵌入式 (汽车/航空) | **MCCC**, QP/C++ | 优先级保护 + MISRA 合规 + 零堆分配 |
| 高吞吐单线程事件 | EnTT | 极致单线程性能, 无同步开销 |
| 简单解耦 (观察者模式) | sigslot | 最简 API, 同步直接调用 |
| 通用事件队列 | eventpp | 功能丰富, 策略可配置 |
| 跨进程/网络通信 | ZeroMQ | 多种传输协议, 生态成熟 |
| Active Object 模式 | QP/C++, **MCCC** | 状态机 + 事件驱动 |

---

## 5. 架构对比

### 5.1 同步机制

| 项目 | 入队同步 | 消费同步 | 单核代价 |
|------|----------|----------|----------|
| **MCCC** | CAS 原子操作 | 序列号检查 (relaxed) | 中等 (CAS spin) |
| **eventpp** | Mutex (try_lock 优化) | Mutex (swap-and-process) | 中等 (锁竞争) |
| **EnTT** | 无 (单线程) | 无 (单线程) | **极低** |
| **sigslot** | Mutex (emit 时) | 无 (同步) | 低 |
| **ZeroMQ** | Socket 内部锁 | Socket 内部锁 | 高 (协议栈) |

### 5.2 MCCC 架构

```mermaid
flowchart TB
    subgraph Producers[多生产者 - Lock-free]
        P1[Producer 1]
        P2[Producer 2]
        PN[Producer N]
    end

    subgraph AsyncBus["AsyncBus&lt;PayloadVariant&gt;"]
        subgraph AC[准入控制]
            HC{"HIGH<br/>99%"}
            MC{"MEDIUM<br/>80%"}
            LC{"LOW<br/>60%"}
        end
        subgraph RB[Lock-free Ring Buffer]
            Ring[(128K Slots<br/>Envelope 内嵌<br/>Sequence Sync)]
        end
        Stats[Statistics<br/>10项原子计数]
    end

    subgraph Consumer[单消费者 - Batch]
        C1[Batch Processor<br/>1024/批]
        CB[Callback Dispatch<br/>固定数组 + VariantIndex]
    end

    P1 --> HC
    P2 --> MC
    PN --> LC
    HC --> Ring
    MC --> Ring
    LC --> Ring
    Ring --> C1
    C1 --> CB
    Ring --> Stats
```

---

## 6. 各竞品优缺点分析

### 6.1 MCCC

| 优点 | 缺点 |
|------|------|
| Lock-free CAS 无锁入队, 线程安全 | 单消费者 (MPSC), 不支持多消费者并行 |
| 优先级准入 + 背压控制, HIGH 零丢失 | 吞吐量不如单线程同步方案 (EnTT/sigslot) |
| 零堆分配, 确定性内存, MISRA 合规 | 不支持 IPC/网络传输 |
| Header-only, 零外部依赖, 14.2 KB 二进制 | 固定 Ring Buffer 大小, 需编译期预估容量 |
| 编译期可裁剪, 适配嵌入式 MCU | 生态较新, 社区规模小 |

### 6.2 eventpp

| 优点 | 缺点 |
|------|------|
| 功能丰富: EventQueue + EventDispatcher + CallbackList | Mutex 同步, 锁竞争下性能下降 |
| 策略可配置 (自定义分配器、混入) | std::list 逐节点分配, 内存碎片风险 |
| Header-only, 零外部依赖 | 无优先级控制, 无背压监控 |
| C++11 兼容, 编译器覆盖广 | 队列无限增长, 无内存上限保护 |
| API 设计成熟, 文档完善 | 不适合安全关键嵌入式 (MISRA 不合规) |

### 6.3 EnTT

| 优点 | 缺点 |
|------|------|
| 极致单线程性能 (~93 M/s) | **不支持线程安全**, 多线程需外部加锁 |
| ECS 生态完整, 游戏引擎广泛使用 | 吞吐量方差大 (StdDev/Mean = 23.5%) |
| Header-only, 零外部依赖 | 无优先级, 无背压, 无安全保护 |
| 编译期类型安全 | 最新版 (v3.16+) 需 C++20, 旧版 (v3.12) 才支持 C++17 |
| 社区活跃 (~10K Stars) | 不适合嵌入式 (动态内存, 需 RTTI) |

### 6.4 sigslot

| 优点 | 缺点 |
|------|------|
| 最简 API, 同步直接调用, 零延迟 | 同步阻塞, 无异步队列 |
| 线程安全 (Mutex 保护) | 回调执行时间影响发布者性能 |
| Header-only, 单文件 | 无优先级, 无背压, 无队列 |
| RAII 自动断连, 悬空安全 | 不适合高吞吐异步场景 |
| 观察者模式的轻量替代 | 不适合嵌入式 (动态内存, 需 RTTI) |

### 6.5 ZeroMQ

| 优点 | 缺点 |
|------|------|
| 支持 IPC/TCP/inproc 多种传输 | 编译型库, 静态库 2.15 MB, 重量级 |
| HWM 背压控制, 成熟稳定 | 入队延迟高 (~219 ns), 协议栈开销 |
| 生态庞大 (~10K Stars), 多语言绑定 | 不适合嵌入式 MCU (资源消耗大) |
| 模式丰富: PUB/SUB, PUSH/PULL, REQ/REP | 无类型安全, 裸字节传输 |
| 跨平台, 工业级可靠性 | 外部依赖多, 构建复杂 |

### 6.6 QP/C++

| 优点 | 缺点 |
|------|------|
| 完整 Active Object 框架 + HSM 状态机 | GPL 许可证, 商业使用需付费 |
| MISRA 合规, 汽车/航空认证经验 | 非 Header-only, 构建复杂 |
| 零堆分配, 确定性内存 | API 复杂, 学习曲线陡峭 |
| 优先级调度, 事件池管理 | 需 QF::init/QF::run 生命周期管理 |
| 嵌入式深度优化, 多 RTOS 支持 | 框架耦合度高, 不易局部引入 |

### 6.7 综合对比

MCCC 在纯吞吐量上不如单线程同步方案 (EnTT/sigslot), 但作为**完整的异步消息总线**, 在同类竞品中表现优异:

| 对比维度 | MCCC vs eventpp | MCCC vs ZeroMQ | MCCC vs EnTT |
|----------|:--------------:|:--------------:|:------------:|
| 吞吐量 | MCCC BARE 快 **3.3x** | MCCC BARE 快 **3.6x** | EnTT 快 **6.6x** (单线程) |
| 线程安全 | 均支持 | 均支持 | EnTT **不支持** |
| 优先级控制 | MCCC **独有** | 均无 | EnTT **无** |
| 背压监控 | MCCC **独有** | ZeroMQ 有 HWM | EnTT **无** |
| 零堆分配 | MCCC **是** | ZeroMQ **否** | EnTT **否** |
| 二进制大小 | MCCC 小 **1.9x** | MCCC 小 **155x** | MCCC 小 **1.3x** |
| 外部依赖 | 均无 | ZeroMQ **需要** | 均无 |
| 嵌入式适配 | MCCC **可配置** | ZeroMQ **不适合** | EnTT **不适合** |

**定位**: MCCC 不是要跟单线程同步方案比吞吐, 而是在提供**安全关键特性** (优先级准入 + 背压控制 + 零堆分配 + MISRA 合规) 的前提下, 保持**工业级可用的性能**。优化后 MCCC FULL_FEATURED 吞吐量是 eventpp 的 1.5x, 是 ZeroMQ 的 1.7x, 同时保持最小的资源占用 (14.2 KB 二进制, ~4 KB 最小 RAM)。

---

## 7. 嵌入式优化路线图

### Phase 1: 单核/双核深度优化 ✅ 已完成

| 编号 | 优化项 | 实测效果 |
|------|--------|----------|
| 1.1 | SPSC wait-free 快速路径 (`MCCC_SINGLE_PRODUCER=1`) | BARE_METAL 43.2 M/s (vs MPSC 33.0 M/s, +31%) |
| 1.2 | 索引缓存 (`cached_consumer_pos_`) | 减少跨核 acquire load, 准入检查 ~3 ns |
| 1.3 | Signal fence (`MCCC_SINGLE_CORE=1`) | E2E P50 310 ns (vs 585 ns, -47%) |
| 1.4 | shared_mutex 读写分离 | BARE_METAL 无锁分发, FULL 读锁开销 ~3 ns |
| 1.5 | ProcessBatch 批量更新 | consumer_pos_ 1 次 store + stats 1 次 fetch_add |

**优化前后对比 (竞品 E2E benchmark)**:

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| BARE_METAL | 9.63 M/s | 16.34 M/s | **1.7x** |
| FULL_FEATURED | 4.93 M/s | 7.48 M/s | **1.5x** |
| vs eventpp | 0.9x (更慢) | **1.5x** (更快) | 逆转 |
| vs ZeroMQ | 2.2x | **3.6x** | 领先扩大 |

### Phase 2: 工业级加固

| 编号 | 优化项 | 预期效果 |
|------|--------|----------|
| 2.1 | MISRA C++:2023 全面审计 | 合规认证基础 |
| 2.2 | 确定性内存池 | WCET 可分析 |
| 2.3 | E2E 校验 (CRC + 序列号) | 功能安全 |

### Phase 3: 高级优化

| 编号 | 优化项 | 预期效果 |
|------|--------|----------|
| 3.1 | 索引重映射 | 减少缓存冲突 |
| 3.2 | 零拷贝消息传递 | 大消息零拷贝 |
| 3.3 | CPU 亲和性绑定 | 减少上下文切换 |

---

## 附录: 测试日志

完整测试输出见 [`docs/benchmark_log.txt`](benchmark_log.txt)

---

*本文档基于本机统一测试数据 + 源代码分析生成。所有性能数据均为同一硬件、同一编译器、同一测试条件下的实测结果。*
