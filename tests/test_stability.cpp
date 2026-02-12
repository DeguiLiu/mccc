/**
 * @file test_stability.cpp
 * @brief Long-running stability and throughput consistency tests.
 *
 * Inspired by eventpp benchmark patterns:
 * - Warmup rounds + measurement rounds
 * - Statistical analysis (mean, stddev, min, max)
 * - Sustained throughput over time
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/message_bus.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>

// ============================================================================
// Test types
// ============================================================================

struct StabMsg { uint32_t seq; };
using StabPayload = std::variant<StabMsg>;
using StabBus = mccc::AsyncBus<StabPayload>;
using StabEnvelope = mccc::MessageEnvelope<StabPayload>;

static void DrainStabBus(StabBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

// ============================================================================
// Statistical helpers
// ============================================================================

struct Stats {
  double mean;
  double stddev;
  double min_val;
  double max_val;
  double p50;
  double p95;
  double p99;
};

static Stats ComputeStats(std::vector<double>& data) {
  Stats s{};
  if (data.empty()) return s;

  std::sort(data.begin(), data.end());

  s.min_val = data.front();
  s.max_val = data.back();
  s.mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());

  double variance = 0.0;
  for (double v : data) {
    double diff = v - s.mean;
    variance += diff * diff;
  }
  s.stddev = std::sqrt(variance / static_cast<double>(data.size()));

  auto percentile = [&data](double p) -> double {
    size_t idx = static_cast<size_t>(p * static_cast<double>(data.size() - 1));
    return data[idx];
  };

  s.p50 = percentile(0.50);
  s.p95 = percentile(0.95);
  s.p99 = percentile(0.99);

  return s;
}

// ============================================================================
// Throughput stability tests
// ============================================================================

TEST_CASE("Throughput stability over 10 rounds (BARE_METAL)", "[Stability]") {
  auto& bus = StabBus::Instance();
  bus.SetPerformanceMode(StabBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainStabBus(bus);

  auto handle = bus.Subscribe<StabMsg>([](const StabEnvelope&) {});

  constexpr uint32_t MSGS_PER_ROUND = 100000U;
  constexpr uint32_t WARMUP_ROUNDS = 3U;
  constexpr uint32_t MEASURE_ROUNDS = 10U;

  std::vector<double> throughputs;

  // Consumer thread
  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    while (bus.ProcessBatch() > 0U) {}
  });

  for (uint32_t round = 0U; round < WARMUP_ROUNDS + MEASURE_ROUNDS; ++round) {
    auto start = std::chrono::steady_clock::now();

    uint32_t published = 0U;
    for (uint32_t i = 0U; i < MSGS_PER_ROUND; ++i) {
      StabMsg msg{i};
      if (bus.Publish(std::move(msg), 1U)) {
        ++published;
      }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double throughput_mps = static_cast<double>(published) / (static_cast<double>(elapsed_ns) / 1e9) / 1e6;

    if (round >= WARMUP_ROUNDS) {
      throughputs.push_back(throughput_mps);
    }

    // Let consumer catch up between rounds
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  stop.store(true, std::memory_order_release);
  consumer.join();

  Stats s = ComputeStats(throughputs);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(StabBus::PerformanceMode::FULL_FEATURED);

  INFO("Throughput (M/s): mean=" << s.mean << " stddev=" << s.stddev
       << " min=" << s.min_val << " max=" << s.max_val
       << " p50=" << s.p50 << " p95=" << s.p95);

  // Coefficient of variation should be < 50% (stable throughput)
  double cv = (s.mean > 0.0) ? (s.stddev / s.mean) : 1.0;
  INFO("CV (coefficient of variation): " << cv);
  REQUIRE(cv < 0.5);

  // Should achieve > 1 M/s in BARE_METAL mode
  REQUIRE(s.mean > 1.0);
}

TEST_CASE("Throughput stability over 10 rounds (FULL_FEATURED)", "[Stability]") {
  auto& bus = StabBus::Instance();
  bus.SetPerformanceMode(StabBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainStabBus(bus);

  auto handle = bus.Subscribe<StabMsg>([](const StabEnvelope&) {});

  constexpr uint32_t MSGS_PER_ROUND = 50000U;
  constexpr uint32_t WARMUP_ROUNDS = 3U;
  constexpr uint32_t MEASURE_ROUNDS = 10U;

  std::vector<double> throughputs;

  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    while (bus.ProcessBatch() > 0U) {}
  });

  for (uint32_t round = 0U; round < WARMUP_ROUNDS + MEASURE_ROUNDS; ++round) {
    auto start = std::chrono::steady_clock::now();

    uint32_t published = 0U;
    for (uint32_t i = 0U; i < MSGS_PER_ROUND; ++i) {
      StabMsg msg{i};
      if (bus.Publish(std::move(msg), 1U)) {
        ++published;
      }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double throughput_mps = static_cast<double>(published) / (static_cast<double>(elapsed_ns) / 1e9) / 1e6;

    if (round >= WARMUP_ROUNDS) {
      throughputs.push_back(throughput_mps);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  stop.store(true, std::memory_order_release);
  consumer.join();

  Stats s = ComputeStats(throughputs);

  bus.Unsubscribe(handle);

  INFO("Throughput (M/s): mean=" << s.mean << " stddev=" << s.stddev
       << " min=" << s.min_val << " max=" << s.max_val);

  double cv = (s.mean > 0.0) ? (s.stddev / s.mean) : 1.0;
  INFO("CV: " << cv);
  REQUIRE(cv < 0.5);

  // FULL_FEATURED should still achieve > 0.5 M/s
  REQUIRE(s.mean > 0.5);
}

TEST_CASE("Sustained throughput - 2 seconds continuous", "[Stability]") {
  auto& bus = StabBus::Instance();
  bus.SetPerformanceMode(StabBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainStabBus(bus);

  std::atomic<uint64_t> consumed{0U};

  auto handle = bus.Subscribe<StabMsg>([&consumed](const StabEnvelope&) {
    consumed.fetch_add(1U, std::memory_order_relaxed);
  });

  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    while (bus.ProcessBatch() > 0U) {}
  });

  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::seconds(2);

  uint64_t published = 0U;
  uint32_t seq = 0U;
  while (std::chrono::steady_clock::now() < deadline) {
    StabMsg msg{seq++};
    if (bus.Publish(std::move(msg), 1U)) {
      ++published;
    }
  }

  // Let consumer finish, then join before any assertions
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  stop.store(true, std::memory_order_release);
  consumer.join();

  auto elapsed = std::chrono::steady_clock::now() - start;
  double elapsed_s = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
  double throughput = static_cast<double>(published) / elapsed_s / 1e6;

  auto stats = bus.GetStatistics();

  INFO("Published: " << published);
  INFO("Consumed: " << consumed.load());
  INFO("Stats.published: " << stats.messages_published);
  INFO("Stats.processed: " << stats.messages_processed);
  INFO("Throughput: " << throughput << " M/s over " << elapsed_s << " s");

  bus.Unsubscribe(handle);

  // Should have published a meaningful number
  REQUIRE(published > 100000U);
  // All successfully published messages should be processed
  REQUIRE(consumed.load() == stats.messages_processed);
}

TEST_CASE("Enqueue latency percentiles (10K samples)", "[Stability]") {
  auto& bus = StabBus::Instance();
  bus.SetPerformanceMode(StabBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainStabBus(bus);

  auto handle = bus.Subscribe<StabMsg>([](const StabEnvelope&) {});

  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    while (bus.ProcessBatch() > 0U) {}
  });

  constexpr uint32_t WARMUP = 1000U;
  constexpr uint32_t SAMPLES = 10000U;

  // Warmup
  for (uint32_t i = 0U; i < WARMUP; ++i) {
    StabMsg msg{i};
    bus.Publish(std::move(msg), 1U);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Measure
  std::vector<double> latencies;
  latencies.reserve(SAMPLES);

  for (uint32_t i = 0U; i < SAMPLES; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    StabMsg msg{i};
    bus.Publish(std::move(msg), 1U);
    auto t1 = std::chrono::steady_clock::now();
    latencies.push_back(
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }

  stop.store(true, std::memory_order_release);
  consumer.join();

  Stats s = ComputeStats(latencies);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(StabBus::PerformanceMode::FULL_FEATURED);

  INFO("Enqueue latency (ns): mean=" << s.mean
       << " p50=" << s.p50 << " p95=" << s.p95 << " p99=" << s.p99
       << " max=" << s.max_val);

  // P50 should be under 1 microsecond
  REQUIRE(s.p50 < 1000.0);
  // P99 should be under 10 microseconds
  REQUIRE(s.p99 < 10000.0);
}
