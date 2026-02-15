/**
 * @file competitive_benchmark.cpp
 * @brief Unified benchmark: MCCC vs eventpp vs EnTT vs sigslot vs ZeroMQ
 *
 * All tests run on the same machine, same conditions, same message payload.
 * Each benchmark pins threads to specific CPU cores for stable results.
 *
 * Compile (from mccc-bus root):
 *   # MPSC (default):
 *   g++ -std=c++17 -O3 -march=native -o competitive_bench \
 *       examples/competitive_benchmark.cpp \
 *       -Iinclude \
 *       -I../streaming-arch-demo/refs/eventpp/include \
 *       -I../streaming-arch-demo/refs/entt/src \
 *       -I../streaming-arch-demo/refs/sigslot/include \
 *       -I../streaming-arch-demo/refs/cppzmq \
 *       -I../streaming-arch-demo/refs/libzmq/include \
 *       -L../streaming-arch-demo/refs/libzmq/build/lib \
 *       -lzmq -lpthread
 *
 *   # SPSC:
 *   g++ -std=c++17 -O3 -march=native -DMCCC_SINGLE_PRODUCER=1 \
 *       -o competitive_bench_spsc \
 *       examples/competitive_benchmark.cpp \
 *       -Iinclude \
 *       -I../streaming-arch-demo/refs/eventpp/include \
 *       -I../streaming-arch-demo/refs/entt/src \
 *       -I../streaming-arch-demo/refs/sigslot/include \
 *       -I../streaming-arch-demo/refs/cppzmq \
 *       -I../streaming-arch-demo/refs/libzmq/include \
 *       -L../streaming-arch-demo/refs/libzmq/build/lib \
 *       -lzmq -lpthread
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

// --- Competitor headers ---
#include <entt/signal/dispatcher.hpp>
#include <eventpp/eventqueue.h>
#include <eventpp/internal/poolallocator_i.h>
#include <sigslot/signal.hpp>
#include <zmq.hpp>

// --- MCCC ---
#include <mccc/mccc.hpp>

using namespace std::chrono;
using Clock = steady_clock;

// ============================================================================
// CPU Affinity
// ============================================================================
static bool pin_thread(int core) {
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
  (void)core;
  return false;
#endif
}

// ============================================================================
// Test payload — same for all implementations
// ============================================================================
struct TestMsg {
  uint64_t seq;
  float x, y, z, w;
};
// sizeof(TestMsg) = 24

struct TestMsg64 {
  uint64_t seq;
  char data[56];
};
// sizeof(TestMsg64) = 64

struct TestMsg128 {
  uint64_t seq;
  char data[120];
};
// sizeof(TestMsg128) = 128

struct TestMsg256 {
  uint64_t seq;
  char data[248];
};
// sizeof(TestMsg256) = 256

// ============================================================================
// Statistics
// ============================================================================
struct Stats {
  double mean, stddev, p50, p95, p99, min_val, max_val;
};

static Stats compute_stats(std::vector<double>& data) {
  Stats s{};
  if (data.empty())
    return s;
  std::sort(data.begin(), data.end());
  s.min_val = data.front();
  s.max_val = data.back();
  s.mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());
  double var = 0.0;
  for (double v : data) {
    double d = v - s.mean;
    var += d * d;
  }
  s.stddev = std::sqrt(var / static_cast<double>(data.size()));
  auto pct = [&](double p) { return data[static_cast<size_t>(p * (data.size() - 1))]; };
  s.p50 = pct(0.50);
  s.p95 = pct(0.95);
  s.p99 = pct(0.99);
  return s;
}

// ============================================================================
// Config
// ============================================================================
static constexpr uint32_t BENCH_MSGS = 1000000U;
static constexpr uint32_t ROUNDS = 10U;
static constexpr int PRODUCER_CORE = 0;
static constexpr int CONSUMER_CORE = 1;

static void print_header(const char* name) {
  std::printf("\n========== %s ==========\n", name);
}

static void print_result(const char* label, Stats& tp, Stats& lat) {
  std::printf("  %-28s Throughput: %8.2f ± %.2f M/s   Latency: P50=%6.0f P95=%6.0f P99=%6.0f ns\n", label, tp.mean,
              tp.stddev, lat.p50, lat.p95, lat.p99);
}

static void print_throughput_only(const char* label, Stats& tp) {
  std::printf("  %-28s Throughput: %8.2f ± %.2f M/s\n", label, tp.mean, tp.stddev);
}

// ============================================================================
// 1. MCCC BARE_METAL
// ============================================================================
using BenchPayload = std::variant<TestMsg>;
using BenchBus = mccc::AsyncBus<BenchPayload>;

static void bench_mccc_bare() {
  print_header("MCCC BARE_METAL (lock-free MPSC + message bus)");
  auto& bus = BenchBus::Instance();
  bus.SetPerformanceMode(BenchBus::PerformanceMode::BARE_METAL);
  auto handle = bus.Subscribe<TestMsg>([](const mccc::MessageEnvelope<BenchPayload>&) {});

  // E2E throughput: producer thread + consumer thread
  std::vector<double> tps, lats;
  std::vector<double> pub_tps, pub_lats;  // publish-only
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatch() > 0) {}

    std::atomic<bool> stop{false};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      while (!stop.load(std::memory_order_acquire)) {
        bus.ProcessBatch();
      }
      while (bus.ProcessBatch() > 0) {}
    });

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();
    stop.store(true, std::memory_order_release);
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
  }

  // Publish-only throughput (no consumer thread)
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatch() > 0) {}

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    pub_tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    pub_lats.push_back(elapsed_ns / BENCH_MSGS);

    // Drain queue
    while (bus.ProcessBatch() > 0) {}
  }

  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  Stats ptp = compute_stats(pub_tps);
  Stats plat = compute_stats(pub_lats);
  print_result("E2E (pub+consume):", tp, lat);
  print_result("Publish-only:", ptp, plat);
  bus.Unsubscribe(handle);
}

// ============================================================================
// 2. MCCC FULL_FEATURED
// ============================================================================
static void bench_mccc_full() {
  print_header("MCCC FULL_FEATURED (lock-free MPSC + priority + backpressure + stats)");
  auto& bus = BenchBus::Instance();
  bus.SetPerformanceMode(BenchBus::PerformanceMode::FULL_FEATURED);
  auto handle = bus.Subscribe<TestMsg>([](const mccc::MessageEnvelope<BenchPayload>&) {});

  std::vector<double> tps, lats;
  std::vector<double> pub_tps, pub_lats;  // publish-only
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatch() > 0) {}

    std::atomic<bool> stop{false};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      while (!stop.load(std::memory_order_acquire)) {
        bus.ProcessBatch();
      }
      while (bus.ProcessBatch() > 0) {}
    });

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();
    stop.store(true, std::memory_order_release);
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
  }

  // Publish-only throughput (no consumer thread)
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatch() > 0) {}

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    pub_tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    pub_lats.push_back(elapsed_ns / BENCH_MSGS);

    // Drain queue
    while (bus.ProcessBatch() > 0) {}
  }

  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  Stats ptp = compute_stats(pub_tps);
  Stats plat = compute_stats(pub_lats);
  print_result("E2E (pub+consume):", tp, lat);
  print_result("Publish-only:", ptp, plat);
  bus.Unsubscribe(handle);
}

// ============================================================================
// 3. eventpp EventQueue (Raw, default std::list)
// ============================================================================
static void bench_eventpp_raw() {
  print_header("eventpp EventQueue Raw (mutex + std::list)");
  using RawQueue = eventpp::EventQueue<int, void(const TestMsg&)>;
  RawQueue queue;

  std::atomic<uint64_t> processed{0};
  queue.appendListener(1, [&processed](const TestMsg&) { processed.fetch_add(1, std::memory_order_relaxed); });

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    processed.store(0, std::memory_order_relaxed);

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      float fi = static_cast<float>(i);
      queue.enqueue(1, TestMsg{i, fi, fi * 2, fi * 3, fi * 4});
    }
    auto enqueue_end = Clock::now();
    queue.process();
    auto end = Clock::now();

    double enqueue_ns = static_cast<double>(duration_cast<nanoseconds>(enqueue_end - start).count());
    double total_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / total_ns * 1e3);
    lats.push_back(enqueue_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Enqueue+Process:", tp, lat);
}

// ============================================================================
// 4. eventpp EventQueue (PoolQueueList allocator)
// ============================================================================
struct PoolPolicies {
  template <typename T>
  using QueueList = eventpp::PoolQueueList<T, 8192>;
};

static void bench_eventpp_pool() {
  print_header("eventpp EventQueue Pool (mutex + CAS pool allocator)");
  using PoolQueue = eventpp::EventQueue<int, void(const TestMsg&), PoolPolicies>;
  PoolQueue queue;

  std::atomic<uint64_t> processed{0};
  queue.appendListener(1, [&processed](const TestMsg&) { processed.fetch_add(1, std::memory_order_relaxed); });

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    processed.store(0, std::memory_order_relaxed);

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      float fi = static_cast<float>(i);
      queue.enqueue(1, TestMsg{i, fi, fi * 2, fi * 3, fi * 4});
    }
    auto enqueue_end = Clock::now();
    queue.process();
    auto end = Clock::now();

    double enqueue_ns = static_cast<double>(duration_cast<nanoseconds>(enqueue_end - start).count());
    double total_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / total_ns * 1e3);
    lats.push_back(enqueue_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Enqueue+Process:", tp, lat);
}

// ============================================================================
// 4b. eventpp EventQueue (HighPerfPolicy: SpinLock + CAS pool + shared_mutex)
// ============================================================================
static void bench_eventpp_highperf() {
  print_header("eventpp EventQueue HighPerf (SpinLock + CAS pool + shared_mutex)");
  using HiQueue = eventpp::EventQueue<int, void(const TestMsg&), eventpp::HighPerfPolicy>;
  HiQueue queue;

  std::atomic<uint64_t> processed{0};
  queue.appendListener(1, [&processed](const TestMsg&) { processed.fetch_add(1, std::memory_order_relaxed); });

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    processed.store(0, std::memory_order_relaxed);

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      float fi = static_cast<float>(i);
      queue.enqueue(1, TestMsg{i, fi, fi * 2, fi * 3, fi * 4});
    }
    auto enqueue_end = Clock::now();
    queue.process();
    auto end = Clock::now();

    double enqueue_ns = static_cast<double>(duration_cast<nanoseconds>(enqueue_end - start).count());
    double total_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / total_ns * 1e3);
    lats.push_back(enqueue_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Enqueue+Process:", tp, lat);
}

// ============================================================================
// 5. EnTT dispatcher (enqueue + update)
// ============================================================================
static uint64_t g_entt_count = 0;
static void entt_handler(TestMsg&) {
  ++g_entt_count;
}

static void bench_entt() {
  print_header("EnTT dispatcher (single-thread, enqueue + update)");
  entt::dispatcher dispatcher;
  dispatcher.sink<TestMsg>().connect<&entt_handler>();

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    g_entt_count = 0;
    dispatcher.clear<TestMsg>();

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      dispatcher.enqueue<TestMsg>(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f});
    }
    auto enqueue_end = Clock::now();
    dispatcher.update<TestMsg>();
    auto end = Clock::now();

    double enqueue_ns = static_cast<double>(duration_cast<nanoseconds>(enqueue_end - start).count());
    double total_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / total_ns * 1e3);
    lats.push_back(enqueue_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Enqueue+Update:", tp, lat);
}

// ============================================================================
// 6. sigslot (synchronous signal/slot, no queue)
// ============================================================================
static void bench_sigslot() {
  print_header("sigslot (synchronous signal/slot, direct call)");
  sigslot::signal<const TestMsg&> sig;

  std::atomic<uint64_t> processed{0};
  sig.connect([&processed](const TestMsg&) { processed.fetch_add(1, std::memory_order_relaxed); });

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    processed.store(0, std::memory_order_relaxed);

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      sig(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f});
    }
    auto end = Clock::now();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Sync emit:", tp, lat);
}

// ============================================================================
// 7. ZeroMQ inproc:// (pub/sub, socket-based)
// ============================================================================
static void bench_zeromq() {
  print_header("ZeroMQ inproc:// (pub/sub, socket-based IPC)");
  zmq::context_t ctx(1);

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    zmq::socket_t pub(ctx, zmq::socket_type::push);
    zmq::socket_t sub(ctx, zmq::socket_type::pull);

    std::string addr = "inproc://bench_" + std::to_string(r);
    pub.bind(addr);
    sub.connect(addr);

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};

    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      zmq::message_t msg;
      while (!done.load(std::memory_order_acquire) || consumed.load(std::memory_order_relaxed) < BENCH_MSGS) {
        auto result = sub.recv(msg, zmq::recv_flags::dontwait);
        if (result)
          consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      TestMsg m{i, 1.0f, 2.0f, 3.0f, 4.0f};
      zmq::message_t msg(&m, sizeof(m));
      pub.send(msg, zmq::send_flags::none);
    }
    auto end = Clock::now();
    done.store(true, std::memory_order_release);

    // Wait for consumer
    while (consumed.load(std::memory_order_relaxed) < BENCH_MSGS) {
      std::this_thread::yield();
    }
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);

    pub.close();
    sub.close();
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result("Push/Pull inproc:", tp, lat);
}

// ============================================================================
// 8. MCCC ProcessBatchWith (zero-overhead visitor dispatch)
// ============================================================================
static void bench_mccc_visitor() {
  print_header("MCCC ProcessBatchWith (zero-overhead visitor dispatch)");
  auto& bus = BenchBus::Instance();

  auto visitor = mccc::make_overloaded([](const TestMsg&) {});

  // --- BARE_METAL + ProcessBatchWith ---
  bus.SetPerformanceMode(BenchBus::PerformanceMode::BARE_METAL);
  std::vector<double> bare_tps, bare_lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatchWith(visitor) > 0) {}

    std::atomic<bool> stop{false};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      while (!stop.load(std::memory_order_acquire)) {
        bus.ProcessBatchWith(visitor);
      }
      while (bus.ProcessBatchWith(visitor) > 0) {}
    });

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();
    stop.store(true, std::memory_order_release);
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    bare_tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    bare_lats.push_back(elapsed_ns / BENCH_MSGS);
  }
  Stats btp = compute_stats(bare_tps);
  Stats blat = compute_stats(bare_lats);
  print_result("BARE_METAL E2E (Visitor):", btp, blat);

  // --- FULL_FEATURED + ProcessBatchWith ---
  bus.SetPerformanceMode(BenchBus::PerformanceMode::FULL_FEATURED);
  std::vector<double> full_tps, full_lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatchWith(visitor) > 0) {}

    std::atomic<bool> stop{false};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      while (!stop.load(std::memory_order_acquire)) {
        bus.ProcessBatchWith(visitor);
      }
      while (bus.ProcessBatchWith(visitor) > 0) {}
    });

    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
    }
    auto end = Clock::now();
    stop.store(true, std::memory_order_release);
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    full_tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    full_lats.push_back(elapsed_ns / BENCH_MSGS);
  }
  Stats ftp = compute_stats(full_tps);
  Stats flat = compute_stats(full_lats);
  print_result("FULL_FEATURED E2E (Visitor):", ftp, flat);
}

// ============================================================================
// 9. Pub-only throughput comparison (queue overflow analysis)
//    Explains why FULL_FEATURED Pub-only drops from ~28 M/s to ~23 M/s
//    when BENCH_MSGS (1M) >> MCCC_QUEUE_DEPTH (128K).
//
//    Variant A: 100K messages, no consumer (queue never fills)
//    Variant B: 1M messages, consumer draining in background (queue stays unfull)
//    Control:   1M messages, no consumer (original test — queue overflows)
// ============================================================================
static void bench_mccc_pubonly_comparison() {
  print_header("MCCC Pub-only Throughput Comparison (queue overflow analysis)");
  auto& bus = BenchBus::Instance();

  static constexpr uint32_t SMALL_MSGS = 100000U;  // fits in 128K queue

  auto handle = bus.Subscribe<TestMsg>([](const mccc::MessageEnvelope<BenchPayload>&) {});

  auto run_pubonly = [&](const char* label, uint32_t msg_count, bool with_consumer) {
    std::vector<double> tps;
    for (uint32_t r = 0; r < ROUNDS; ++r) {
      bus.ResetStatistics();
      while (bus.ProcessBatch() > 0) {}

      std::atomic<bool> stop{false};
      std::thread consumer_thread;
      if (with_consumer) {
        consumer_thread = std::thread([&]() {
          pin_thread(CONSUMER_CORE);
          while (!stop.load(std::memory_order_acquire)) {
            bus.ProcessBatch();
          }
          while (bus.ProcessBatch() > 0) {}
        });
      }

      pin_thread(PRODUCER_CORE);
      auto start = Clock::now();
      for (uint32_t i = 0; i < msg_count; ++i) {
        bus.Publish(TestMsg{i, 1.0f, 2.0f, 3.0f, 4.0f}, 0U);
      }
      auto end = Clock::now();

      if (with_consumer) {
        stop.store(true, std::memory_order_release);
        consumer_thread.join();
      } else {
        while (bus.ProcessBatch() > 0) {}
      }

      double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
      tps.push_back(static_cast<double>(msg_count) / elapsed_ns * 1e3);
    }
    Stats tp = compute_stats(tps);
    print_throughput_only(label, tp);
  };

  // --- BARE_METAL ---
  bus.SetPerformanceMode(BenchBus::PerformanceMode::BARE_METAL);
  std::printf("\n  [BARE_METAL]\n");
  run_pubonly("Control: 1M no consumer", BENCH_MSGS, false);
  run_pubonly("Variant A: 100K no consumer", SMALL_MSGS, false);
  run_pubonly("Variant B: 1M + consumer drain", BENCH_MSGS, true);

  // --- FULL_FEATURED ---
  bus.SetPerformanceMode(BenchBus::PerformanceMode::FULL_FEATURED);
  std::printf("\n  [FULL_FEATURED]\n");
  run_pubonly("Control: 1M no consumer", BENCH_MSGS, false);
  run_pubonly("Variant A: 100K no consumer", SMALL_MSGS, false);
  run_pubonly("Variant B: 1M + consumer drain", BENCH_MSGS, true);

  bus.Unsubscribe(handle);
}

// ============================================================================
// 10. Multi-size payload benchmark (MCCC vs eventpp vs sigslot vs ZeroMQ)
// ============================================================================

// MCCC multi-size bus types
using Payload64 = std::variant<TestMsg64>;
using Payload128 = std::variant<TestMsg128>;
using Payload256 = std::variant<TestMsg256>;
using Bus64 = mccc::AsyncBus<Payload64>;
using Bus128 = mccc::AsyncBus<Payload128>;
using Bus256 = mccc::AsyncBus<Payload256>;

template <typename MsgT, typename PayloadT, typename BusT>
static void bench_mccc_size(const char* label) {
  auto& bus = BusT::Instance();
  bus.SetPerformanceMode(BusT::PerformanceMode::FULL_FEATURED);
  auto handle = bus.template Subscribe<MsgT>([](const mccc::MessageEnvelope<PayloadT>&) {});

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    bus.ResetStatistics();
    while (bus.ProcessBatch() > 0) {}

    std::atomic<bool> stop{false};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      while (!stop.load(std::memory_order_acquire)) {
        bus.ProcessBatch();
      }
      while (bus.ProcessBatch() > 0) {}
    });

    MsgT msg{};
    msg.seq = 0;
    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      msg.seq = i;
      bus.Publish(MsgT(msg), 0U);
    }
    auto end = Clock::now();
    stop.store(true, std::memory_order_release);
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result(label, tp, lat);
  bus.Unsubscribe(handle);
}

template <typename MsgT>
static void bench_eventpp_size(const char* label) {
  using Queue = eventpp::EventQueue<int, void(const MsgT&)>;
  Queue queue;
  queue.appendListener(1, [](const MsgT&) {});

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    MsgT msg{};
    msg.seq = 0;
    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      msg.seq = i;
      queue.enqueue(1, msg);
    }
    auto enqueue_end = Clock::now();
    queue.process();

    double enqueue_ns = static_cast<double>(duration_cast<nanoseconds>(enqueue_end - start).count());
    double total_ns = static_cast<double>(duration_cast<nanoseconds>(Clock::now() - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / total_ns * 1e3);
    lats.push_back(enqueue_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result(label, tp, lat);
}

template <typename MsgT>
static void bench_sigslot_size(const char* label) {
  sigslot::signal<const MsgT&> sig;
  sig.connect([](const MsgT&) {});

  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    MsgT msg{};
    msg.seq = 0;
    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      msg.seq = i;
      sig(msg);
    }
    auto end = Clock::now();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result(label, tp, lat);
}

template <typename MsgT>
static void bench_zeromq_size(const char* label) {
  zmq::context_t ctx(1);
  std::vector<double> tps, lats;
  for (uint32_t r = 0; r < ROUNDS; ++r) {
    zmq::socket_t pub(ctx, zmq::socket_type::push);
    zmq::socket_t sub(ctx, zmq::socket_type::pull);
    std::string addr = "inproc://size_bench_" + std::to_string(sizeof(MsgT)) + "_" + std::to_string(r);
    pub.bind(addr);
    sub.connect(addr);

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};
    std::thread consumer([&]() {
      pin_thread(CONSUMER_CORE);
      zmq::message_t msg;
      while (!done.load(std::memory_order_acquire) || consumed.load(std::memory_order_relaxed) < BENCH_MSGS) {
        auto result = sub.recv(msg, zmq::recv_flags::dontwait);
        if (result)
          consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });

    MsgT m{};
    m.seq = 0;
    pin_thread(PRODUCER_CORE);
    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      m.seq = i;
      zmq::message_t msg(&m, sizeof(m));
      pub.send(msg, zmq::send_flags::none);
    }
    auto end = Clock::now();
    done.store(true, std::memory_order_release);
    while (consumed.load(std::memory_order_relaxed) < BENCH_MSGS)
      std::this_thread::yield();
    consumer.join();

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);
    lats.push_back(elapsed_ns / BENCH_MSGS);
    pub.close();
    sub.close();
  }
  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);
  print_result(label, tp, lat);
}

static void bench_multi_size() {
  std::printf("\n==========================================\n");
  std::printf("  Multi-size Payload Benchmark\n");
  std::printf("==========================================\n");

  // 64 bytes
  std::printf("\n--- 64 bytes ---\n");
  bench_mccc_size<TestMsg64, Payload64, Bus64>("MCCC FULL 64B:");
  bench_eventpp_size<TestMsg64>("eventpp Raw 64B:");
  bench_sigslot_size<TestMsg64>("sigslot 64B:");
  bench_zeromq_size<TestMsg64>("ZeroMQ 64B:");

  // 128 bytes
  std::printf("\n--- 128 bytes ---\n");
  bench_mccc_size<TestMsg128, Payload128, Bus128>("MCCC FULL 128B:");
  bench_eventpp_size<TestMsg128>("eventpp Raw 128B:");
  bench_sigslot_size<TestMsg128>("sigslot 128B:");
  bench_zeromq_size<TestMsg128>("ZeroMQ 128B:");

  // 256 bytes
  std::printf("\n--- 256 bytes ---\n");
  bench_mccc_size<TestMsg256, Payload256, Bus256>("MCCC FULL 256B:");
  bench_eventpp_size<TestMsg256>("eventpp Raw 256B:");
  bench_sigslot_size<TestMsg256>("sigslot 256B:");
  bench_zeromq_size<TestMsg256>("ZeroMQ 256B:");
}

// ============================================================================
// Main
// ============================================================================
int main() {
  std::printf("========================================\n");
  std::printf("  Competitive Benchmark (Unified)\n");
  std::printf("========================================\n");
  std::printf("  CPU:       Intel Xeon Cascadelake 64 vCPU\n");
  std::printf("  OS:        Ubuntu 24.04 LTS\n");
  std::printf("  Compiler:  GCC 13.3, -O3 -march=native\n");
  std::printf("  Messages:  %u per round, %u rounds\n", BENCH_MSGS, ROUNDS);
  std::printf("  Payload:   TestMsg (24/64/128/256 bytes)\n");
  std::printf("  Affinity:  Producer=core %d, Consumer=core %d\n", PRODUCER_CORE, CONSUMER_CORE);
  std::printf("========================================\n");
  std::printf("  MCCC Config:\n");
  std::printf("    MCCC_SINGLE_PRODUCER = %d\n", MCCC_SINGLE_PRODUCER);
  std::printf("    MCCC_SINGLE_CORE     = %d\n", MCCC_SINGLE_CORE);
  std::printf("    MCCC_QUEUE_DEPTH     = %d\n", MCCC_QUEUE_DEPTH);
  std::printf("  Versions:\n");
  std::printf("    MCCC:    v2.0.0 (mccc-bus)\n");
  std::printf("    eventpp: v0.3.0 (fork: gitee.com/liudegui/eventpp)\n");
  std::printf("    EnTT:    v3.12.2\n");
  std::printf("    sigslot: v1.2.3\n");
  std::printf("    ZeroMQ:  v4.3.5 (libzmq)\n");
  std::printf("========================================\n");

  // Warmup
  std::printf("\n[Warmup] Running warmup rounds...\n");
  {
    sigslot::signal<const TestMsg&> sig;
    sig.connect([](const TestMsg&) {});
    for (int i = 0; i < 100000; ++i)
      sig(TestMsg{0, 0, 0, 0, 0});
  }

  bench_mccc_bare();
  bench_mccc_full();
  bench_mccc_visitor();
  bench_mccc_pubonly_comparison();
  bench_eventpp_raw();
  bench_eventpp_pool();
  bench_eventpp_highperf();
  bench_entt();
  bench_sigslot();
  bench_zeromq();

  bench_multi_size();

  std::printf("\n========================================\n");
  std::printf("  Benchmark Complete!\n");
  std::printf("========================================\n");
  return 0;
}
