/**
 * @file bench_qpcpp.cpp
 * @brief QP/C++ throughput benchmark — comparable to MCCC competitive_benchmark
 *
 * Measures producer-side POST throughput into a QP/C++ Active Object.
 * The QV (cooperative) kernel runs in the main thread; a producer thread
 * posts 1 000 000 static events.  Because the QEQueue counter is uint8_t
 * (max 255 slots), the producer uses postx_() with margin and spins when
 * the queue is full — this is the expected back-pressure path in QP.
 *
 * Build (from mccc-bus root):
 *   see the compile command at the bottom of this file.
 */

// We need QP_IMPL only for the port internals compiled into the library.
// Application code must NOT define QP_IMPL.
#include "qpcpp.hpp"  // QP/C++ framework

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

using namespace std::chrono;
using Clock = steady_clock;

// ============================================================================
// Config
// ============================================================================
static constexpr uint32_t BENCH_MSGS = 1'000'000U;
static constexpr uint32_t ROUNDS = 10U;
static constexpr uint32_t WARMUP = 3U;
static constexpr int PRODUCER_CORE = 0;
static constexpr int CONSUMER_CORE = 1;

// Queue storage — max 254 usable slots with uint8_t counter
static constexpr uint16_t AO_QUEUE_LEN = 200U;

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
// Statistics (same as competitive_benchmark.cpp)
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
  auto pct = [&](double p) { return data[static_cast<size_t>(p * static_cast<double>(data.size() - 1))]; };
  s.p50 = pct(0.50);
  s.p95 = pct(0.95);
  s.p99 = pct(0.99);
  return s;
}

// ============================================================================
// QP/C++ Active Object — minimal benchmark sink
// ============================================================================

// User signal
enum BenchSignals : QP::QSignal { BENCH_SIG = QP::Q_USER_SIG, DONE_SIG, MAX_SIG };

// Static (immutable) event — no dynamic allocation needed
static QP::QEvt const benchEvt(BENCH_SIG);

// Shared state between producer thread and AO
static std::atomic<uint64_t> g_consumed{0};
static std::atomic<bool> g_round_active{false};
static uint32_t g_target_count{0};

// ---- The Active Object ----------------------------------------------------
class BenchAO : public QP::QActive {
 public:
  BenchAO() : QActive(&initial) {}

  static QP::QState initial(void* const me, QP::QEvt const* const e) {
    (void)e;
    return static_cast<BenchAO*>(me)->tran(&running);
  }

  static QP::QState running(void* const me, QP::QEvt const* const e) {
    (void)me;
    switch (e->sig) {
      case BENCH_SIG: {
        uint64_t c = g_consumed.fetch_add(1U, std::memory_order_relaxed) + 1U;
        if (c >= g_target_count && g_round_active.load(std::memory_order_relaxed)) {
          g_round_active.store(false, std::memory_order_release);
        }
        return Q_HANDLED();
      }
      default:
        break;
    }
    return static_cast<BenchAO*>(me)->super(&QP::QAsm::top);
  }
};

static BenchAO l_benchAO;
static QP::QEvt const* l_queueSto[AO_QUEUE_LEN];  // queue storage

// Publish-subscribe storage (required by QF even if unused)
static QP::QSubscrList l_subscrSto[MAX_SIG];

// ============================================================================
// QP/C++ application hooks (required by the framework)
// ============================================================================
void QP::QF::onStartup() {}
void QP::QF::onCleanup() {}

// Called on assertion failure
extern "C" Q_NORETURN Q_onError(char const* const module, int_t const id) {
  std::fprintf(stderr, "QP ASSERTION: module=%s id=%d\n", module, id);
  std::abort();
}

// ============================================================================
// Benchmark driver — runs in a separate thread, posts events into the AO
// ============================================================================
static void producer_thread_func() {
  pin_thread(PRODUCER_CORE);

  // Warmup rounds
  for (uint32_t w = 0; w < WARMUP; ++w) {
    g_consumed.store(0, std::memory_order_relaxed);
    g_target_count = BENCH_MSGS;
    g_round_active.store(true, std::memory_order_release);

    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      // postx_ returns false when queue is full (margin check fails)
      while (!l_benchAO.postx_(&benchEvt, 1U, nullptr)) {
        std::this_thread::yield();
      }
    }
    // Wait for consumer to drain
    while (g_round_active.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  // Measured rounds
  std::vector<double> tps, lats;
  tps.reserve(ROUNDS);
  lats.reserve(ROUNDS);

  for (uint32_t r = 0; r < ROUNDS; ++r) {
    g_consumed.store(0, std::memory_order_relaxed);
    g_target_count = BENCH_MSGS;
    g_round_active.store(true, std::memory_order_release);

    auto start = Clock::now();
    for (uint32_t i = 0; i < BENCH_MSGS; ++i) {
      while (!l_benchAO.postx_(&benchEvt, 1U, nullptr)) {
        std::this_thread::yield();
      }
    }
    auto end = Clock::now();

    // Wait for consumer to drain all events
    while (g_round_active.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end - start).count());
    tps.push_back(static_cast<double>(BENCH_MSGS) / elapsed_ns * 1e3);  // M/s
    lats.push_back(elapsed_ns / static_cast<double>(BENCH_MSGS));       // ns/msg
  }

  Stats tp = compute_stats(tps);
  Stats lat = compute_stats(lats);

  std::printf("\n========== QP/C++ 8.1.2 (POSIX-QV, cooperative) ==========\n");
  std::printf("  %-28s Throughput: %8.2f ± %.2f M/s   Latency: P50=%6.0f P95=%6.0f P99=%6.0f ns\n",
              "E2E (post+dispatch):", tp.mean, tp.stddev, lat.p50, lat.p95, lat.p99);
  std::printf("  Queue depth: %u   Events/round: %u   Rounds: %u\n", AO_QUEUE_LEN, BENCH_MSGS, ROUNDS);
  std::fflush(stdout);

  // Stop the framework
  QP::QF::stop();
}

// ============================================================================
// main
// ============================================================================
int main() {
  // Initialize the QP framework
  QP::QF::init();

  // Initialize publish-subscribe
  QP::QActive::psInit(l_subscrSto, MAX_SIG);

  // Pin the main thread (QF::run event loop) to consumer core
  pin_thread(CONSUMER_CORE);

  // Start the active object (prio=1, no per-AO stack for QV)
  l_benchAO.start(1U,                 // QF priority
                  l_queueSto,         // queue storage
                  Q_DIM(l_queueSto),  // queue length
                  nullptr, 0U         // no per-AO stack (QV kernel)
  );

  // Launch producer in a separate thread
  std::thread producer(producer_thread_func);
  producer.detach();

  // Run the QV cooperative event loop (blocks until QF::stop())
  return QP::QF::run();
}
