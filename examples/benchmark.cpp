/**
 * @file benchmark.cpp
 * @brief MCCC Performance Benchmark with Statistical Analysis
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#include "log_macro.hpp"
#include "bench_utils.hpp"
#include "example_types.hpp"

#include <mccc/component.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace example;
using namespace mccc;
using namespace std::chrono;

namespace config {
constexpr uint32_t WARMUP_ROUNDS = 3U;
constexpr uint32_t TEST_ROUNDS = 10U;
constexpr uint32_t SUSTAINED_DURATION_SEC = 5U;
constexpr uint32_t E2E_LATENCY_SAMPLES = 10000U;
constexpr uint32_t BACKPRESSURE_BURST_SIZE = 150000U;
}  // namespace config

struct Statistics {
  double mean;
  double std_dev;
  double min_val;
  double max_val;
  double p50;
  double p95;
  double p99;
};

Statistics calculate_statistics(const std::vector<double>& data) {
  Statistics stats{};
  if (data.empty()) return stats;

  double sum = std::accumulate(data.begin(), data.end(), 0.0);
  stats.mean = sum / static_cast<double>(data.size());

  double sq_sum = 0.0;
  for (const auto& val : data) {
    sq_sum += (val - stats.mean) * (val - stats.mean);
  }
  stats.std_dev = std::sqrt(sq_sum / static_cast<double>(data.size()));

  auto minmax = std::minmax_element(data.begin(), data.end());
  stats.min_val = *minmax.first;
  stats.max_val = *minmax.second;

  std::vector<double> sorted_data = data;
  std::sort(sorted_data.begin(), sorted_data.end());
  size_t n = sorted_data.size();
  stats.p50 = sorted_data[n * 50 / 100];
  stats.p95 = sorted_data[std::min(n * 95 / 100, n - 1)];
  stats.p99 = sorted_data[std::min(n * 99 / 100, n - 1)];

  return stats;
}

struct BenchmarkResult {
  uint64_t messages_sent;
  uint64_t messages_dropped;
  double publish_time_us;
  double throughput_mps;
  double avg_latency_ns;
};

namespace e2e {
std::atomic<uint64_t> callback_timestamp_ns{0U};
std::atomic<bool> measurement_ready{false};
}  // namespace e2e

class BenchmarkConsumer : public ExampleComponent {
 public:
  static std::shared_ptr<BenchmarkConsumer> create() noexcept {
    std::shared_ptr<BenchmarkConsumer> ptr(new BenchmarkConsumer());
    ptr->init();
    return ptr;
  }

  uint64_t get_processed_count() const noexcept { return processed_count_.load(std::memory_order_relaxed); }
  void reset_count() noexcept { processed_count_.store(0U, std::memory_order_relaxed); }

 private:
  BenchmarkConsumer() noexcept = default;

  void init() noexcept {
    InitializeComponent();
    SubscribeSafe<MotionData>(
        [](std::shared_ptr<ExampleComponent> self_base, const MotionData& data, const MessageHeader& header) noexcept {
          auto self = std::static_pointer_cast<BenchmarkConsumer>(self_base);
          if (self) self->on_motion(data, header);
        });
  }

  void on_motion(const MotionData& /*data*/, const MessageHeader& /*header*/) noexcept {
    processed_count_.fetch_add(1U, std::memory_order_relaxed);
    if (!e2e::measurement_ready.load(std::memory_order_relaxed)) {
      e2e::callback_timestamp_ns.store(
          static_cast<uint64_t>(duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count()),
          std::memory_order_release);
      e2e::measurement_ready.store(true, std::memory_order_release);
    }
  }

  std::atomic<uint64_t> processed_count_{0U};
};

BenchmarkResult run_single_benchmark(uint32_t message_count) {
  ExampleBus::Instance().ResetStatistics();

  uint64_t timestamp_us = static_cast<uint64_t>(
      duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());

  auto start = high_resolution_clock::now();

  for (uint32_t i = 0U; i < message_count; ++i) {
    float fi = static_cast<float>(i);
    MotionData motion(fi * 0.1f, fi * 0.2f, fi * 0.3f, fi * 0.01f);
    if ((i % 100U) == 0U) {
      timestamp_us = static_cast<uint64_t>(
          duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    }
    ExampleBus::Instance().PublishFast(std::move(motion), 100U, timestamp_us);
  }

  auto end = high_resolution_clock::now();
  auto duration_ns = duration_cast<nanoseconds>(end - start).count();

  BusStatisticsSnapshot stats = ExampleBus::Instance().GetStatistics();

  BenchmarkResult result;
  result.messages_sent = message_count;
  result.messages_dropped = stats.messages_dropped;
  result.publish_time_us = static_cast<double>(duration_ns) / 1000.0;
  result.throughput_mps = (static_cast<double>(message_count) / static_cast<double>(duration_ns)) * 1000.0;
  result.avg_latency_ns = static_cast<double>(duration_ns) / static_cast<double>(message_count);

  return result;
}

void run_benchmark_with_stats(const char* name, uint32_t message_count, uint32_t rounds) {
  std::vector<double> throughputs;
  std::vector<double> latencies;
  throughputs.reserve(rounds);
  latencies.reserve(rounds);

  LOG_INFO("");
  LOG_INFO("========== %s (%u messages, %u rounds) ==========", name, message_count, rounds);

  for (uint32_t r = 0U; r < rounds; ++r) {
    BenchmarkResult result = run_single_benchmark(message_count);
    throughputs.push_back(result.throughput_mps);
    latencies.push_back(result.avg_latency_ns);
    std::this_thread::sleep_for(milliseconds(50));
  }

  Statistics tp_stats = calculate_statistics(throughputs);
  Statistics lat_stats = calculate_statistics(latencies);

  LOG_INFO("[%s] Throughput: %.2f +/- %.2f M msg/s", name, tp_stats.mean, tp_stats.std_dev);
  LOG_INFO("[%s] Latency:   %.2f +/- %.2f ns/msg", name, lat_stats.mean, lat_stats.std_dev);
}

void run_e2e_latency_test(uint32_t samples) {
  LOG_INFO("");
  LOG_INFO("========== End-to-End Latency Test (%u samples) ==========", samples);

  std::vector<double> latencies;
  latencies.reserve(samples);

  for (uint32_t i = 0U; i < samples; ++i) {
    e2e::measurement_ready.store(false, std::memory_order_release);
    e2e::callback_timestamp_ns.store(0U, std::memory_order_release);

    uint64_t publish_ns = static_cast<uint64_t>(
        duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count());

    MotionData motion(1.0f, 2.0f, 3.0f, 4.0f);
    ExampleBus::Instance().Publish(std::move(motion), 100U);

    uint32_t wait_count = 0U;
    while (!e2e::measurement_ready.load(std::memory_order_acquire) && wait_count < 10000U) {
      std::this_thread::yield();
      ++wait_count;
    }

    if (e2e::measurement_ready.load(std::memory_order_acquire)) {
      uint64_t callback_ns = e2e::callback_timestamp_ns.load(std::memory_order_acquire);
      latencies.push_back(static_cast<double>(callback_ns - publish_ns));
    }

    if ((i % 100U) == 0U) {
      std::this_thread::sleep_for(microseconds(10));
    }
  }

  if (latencies.empty()) {
    LOG_INFO("[E2E Latency] No valid samples collected!");
    return;
  }

  Statistics stats = calculate_statistics(latencies);
  LOG_INFO("[E2E Latency] Mean=%.2f StdDev=%.2f P50=%.2f P95=%.2f P99=%.2f Max=%.2f ns",
           stats.mean, stats.std_dev, stats.p50, stats.p95, stats.p99, stats.max_val);
}

void run_performance_mode_comparison(uint32_t message_count, uint32_t rounds) {
  LOG_INFO("");
  LOG_INFO("========== Performance Mode Comparison ==========");

  ExampleBus::Instance().SetPerformanceMode(ExampleBus::PerformanceMode::FULL_FEATURED);
  std::vector<double> full_tp, full_lat;
  for (uint32_t r = 0U; r < rounds; ++r) {
    BenchmarkResult result = run_single_benchmark(message_count);
    full_tp.push_back(result.throughput_mps);
    full_lat.push_back(result.avg_latency_ns);
    std::this_thread::sleep_for(milliseconds(50));
  }
  Statistics ftp = calculate_statistics(full_tp);
  Statistics flat = calculate_statistics(full_lat);
  LOG_INFO("FULL_FEATURED: %.2f +/- %.2f M/s, %.2f +/- %.2f ns", ftp.mean, ftp.std_dev, flat.mean, flat.std_dev);

  ExampleBus::Instance().SetPerformanceMode(ExampleBus::PerformanceMode::BARE_METAL);
  std::vector<double> bare_tp, bare_lat;
  for (uint32_t r = 0U; r < rounds; ++r) {
    BenchmarkResult result = run_single_benchmark(message_count);
    bare_tp.push_back(result.throughput_mps);
    bare_lat.push_back(result.avg_latency_ns);
    std::this_thread::sleep_for(milliseconds(50));
  }
  Statistics btp = calculate_statistics(bare_tp);
  Statistics blat = calculate_statistics(bare_lat);
  LOG_INFO("BARE_METAL:    %.2f +/- %.2f M/s, %.2f +/- %.2f ns", btp.mean, btp.std_dev, blat.mean, blat.std_dev);
  LOG_INFO("Feature overhead: %.2f ns/msg", flat.mean - blat.mean);

  ExampleBus::Instance().SetPerformanceMode(ExampleBus::PerformanceMode::FULL_FEATURED);
}

void run_backpressure_test(uint32_t burst_size, std::atomic<bool>& pause_worker) {
  LOG_INFO("");
  LOG_INFO("========== Backpressure Stress Test ==========");

  ExampleBus::Instance().SetPerformanceMode(ExampleBus::PerformanceMode::FULL_FEATURED);

  while (ExampleBus::Instance().QueueDepth() > 0U) {
    std::this_thread::sleep_for(milliseconds(10));
  }
  ExampleBus::Instance().ResetStatistics();

  pause_worker.store(true, std::memory_order_release);
  std::this_thread::sleep_for(milliseconds(50));

  uint32_t high_sent = 0U, high_dropped = 0U;
  uint32_t medium_sent = 0U, medium_dropped = 0U;
  uint32_t low_sent = 0U, low_dropped = 0U;

  for (uint32_t i = 0U; i < burst_size; ++i) {
    MotionData motion(1.0f, 2.0f, 3.0f, 4.0f);
    MessagePriority priority;

    if ((i % 10U) < 2U) {
      priority = MessagePriority::HIGH;
      if (ExampleBus::Instance().PublishWithPriority(std::move(motion), 100U, priority)) ++high_sent;
      else ++high_dropped;
    } else if ((i % 10U) < 5U) {
      priority = MessagePriority::MEDIUM;
      if (ExampleBus::Instance().PublishWithPriority(std::move(motion), 100U, priority)) ++medium_sent;
      else ++medium_dropped;
    } else {
      priority = MessagePriority::LOW;
      if (ExampleBus::Instance().PublishWithPriority(std::move(motion), 100U, priority)) ++low_sent;
      else ++low_dropped;
    }
  }

  pause_worker.store(false, std::memory_order_release);
  std::this_thread::sleep_for(milliseconds(500));

  double high_rate = (high_sent + high_dropped) > 0U ? (100.0 * high_dropped / (high_sent + high_dropped)) : 0.0;
  double medium_rate = (medium_sent + medium_dropped) > 0U ? (100.0 * medium_dropped / (medium_sent + medium_dropped)) : 0.0;
  double low_rate = (low_sent + low_dropped) > 0U ? (100.0 * low_dropped / (low_sent + low_dropped)) : 0.0;

  LOG_INFO("HIGH:   sent=%u, dropped=%u (%.1f%%)", high_sent, high_dropped, high_rate);
  LOG_INFO("MEDIUM: sent=%u, dropped=%u (%.1f%%)", medium_sent, medium_dropped, medium_rate);
  LOG_INFO("LOW:    sent=%u, dropped=%u (%.1f%%)", low_sent, low_dropped, low_rate);

  if ((low_rate >= medium_rate) && (medium_rate >= high_rate) && (low_dropped > 0U)) {
    LOG_INFO("[PASS] Priority-based admission control verified!");
  }
}

void run_sustained_test(uint32_t duration_sec) {
  LOG_INFO("");
  LOG_INFO("========== Sustained Throughput (%u seconds) ==========", duration_sec);

  ExampleBus::Instance().ResetStatistics();
  auto start = high_resolution_clock::now();
  uint64_t sent_count = 0U;

  while (duration_cast<seconds>(high_resolution_clock::now() - start).count() < static_cast<int64_t>(duration_sec)) {
    MotionData motion(1.0f, 2.0f, 3.0f, 4.0f);
    if (ExampleBus::Instance().Publish(std::move(motion), 100U)) ++sent_count;
  }

  auto end = high_resolution_clock::now();
  auto duration_us = duration_cast<microseconds>(end - start).count();
  std::this_thread::sleep_for(milliseconds(500));

  BusStatisticsSnapshot stats = ExampleBus::Instance().GetStatistics();
  LOG_INFO("Duration: %.2f s, Sent: %lu, Processed: %lu, Dropped: %lu, Throughput: %.2f M/s",
           static_cast<double>(duration_us) / 1e6, sent_count, stats.messages_processed,
           stats.messages_dropped, (static_cast<double>(sent_count) / static_cast<double>(duration_us)));
}

int main() {
  LOG_INFO("========================================");
  LOG_INFO("   MCCC Performance Benchmark");
  LOG_INFO("========================================");
  LOG_INFO("Queue capacity: %u", ExampleBus::MAX_QUEUE_DEPTH);
  LOG_INFO("MCCC_SINGLE_PRODUCER=%d, MCCC_SINGLE_CORE=%d",
           MCCC_SINGLE_PRODUCER, MCCC_SINGLE_CORE);

  if (bench::pin_thread_to_core(0)) {
    LOG_INFO("CPU affinity: core 0 (producer)");
  }

  std::atomic<bool> stop_worker{false};
  std::atomic<bool> pause_worker{false};

  std::thread worker([&stop_worker, &pause_worker]() noexcept {
    bench::pin_thread_to_core(1);
    while (!stop_worker.load(std::memory_order_acquire)) {
      if (pause_worker.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(milliseconds(1));
        continue;
      }
      ExampleBus::Instance().ProcessBatch();
    }
    while (ExampleBus::Instance().ProcessBatch() > 0U) {}
  });

  auto consumer = BenchmarkConsumer::create();

  // Warmup
  for (uint32_t w = 0U; w < config::WARMUP_ROUNDS; ++w) {
    run_single_benchmark(10000U);
    std::this_thread::sleep_for(milliseconds(100));
  }
  ExampleBus::Instance().ResetStatistics();

  run_performance_mode_comparison(100000U, config::TEST_ROUNDS);
  run_benchmark_with_stats("Small Batch", 1000U, config::TEST_ROUNDS);
  run_benchmark_with_stats("Medium Batch", 10000U, config::TEST_ROUNDS);
  run_benchmark_with_stats("Large Batch", 100000U, config::TEST_ROUNDS);
  run_e2e_latency_test(config::E2E_LATENCY_SAMPLES);
  run_backpressure_test(config::BACKPRESSURE_BURST_SIZE, pause_worker);
  run_sustained_test(config::SUSTAINED_DURATION_SEC);

  stop_worker.store(true, std::memory_order_release);
  worker.join();

  LOG_INFO("");
  LOG_INFO("Benchmark Completed!");
  return 0;
}
