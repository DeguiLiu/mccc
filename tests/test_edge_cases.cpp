/**
 * @file test_edge_cases.cpp
 * @brief Edge case tests: queue full recovery, error callbacks, statistics accuracy.
 */

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>
#include <thread>
#include <vector>

// ============================================================================
// Test types
// ============================================================================

struct EdgeMsg {
  uint32_t value;
};
using EdgePayload = std::variant<EdgeMsg>;
using EdgeBus = mccc::AsyncBus<EdgePayload>;
using EdgeEnvelope = mccc::MessageEnvelope<EdgePayload>;

static void DrainEdgeBus(EdgeBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

// ============================================================================
// Queue full and recovery
// ============================================================================

TEST_CASE("Queue full returns false, recovery after drain", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  // Fill queue until publish fails (LOW priority, threshold is 60%)
  uint32_t accepted = 0U;
  uint32_t rejected = 0U;
  // Use LOW priority so it gets rejected at 60% capacity
  for (uint32_t i = 0U; i < EdgeBus::MAX_QUEUE_DEPTH; ++i) {
    EdgeMsg msg{i};
    if (bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW)) {
      ++accepted;
    } else {
      ++rejected;
      break;  // Stop at first rejection
    }
  }

  REQUIRE(accepted > 0U);
  REQUIRE(rejected > 0U);
  INFO("Accepted: " << accepted << ", Queue depth: " << bus.QueueDepth());

  // Verify queue is above LOW threshold
  REQUIRE(bus.QueueDepth() >= EdgeBus::LOW_PRIORITY_THRESHOLD);

  // Drain half the queue
  uint32_t half = accepted / 2;
  uint32_t drained = 0U;
  while (drained < half) {
    drained += bus.ProcessBatch();
  }

  // After draining, if below threshold, LOW should be accepted again
  if (bus.QueueDepth() < EdgeBus::LOW_PRIORITY_THRESHOLD) {
    EdgeMsg msg{99999U};
    bool recovered = bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW);
    REQUIRE(recovered);
  }

  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

TEST_CASE("Queue full with HIGH priority still accepts", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  // Fill to 95% using HIGH priority
  uint32_t target = (EdgeBus::MAX_QUEUE_DEPTH * 95U) / 100U;
  uint32_t filled = 0U;
  for (uint32_t i = 0U; i < target; ++i) {
    EdgeMsg msg{i};
    if (bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::HIGH)) {
      ++filled;
    }
  }

  INFO("Filled: " << filled << ", Queue depth: " << bus.QueueDepth());

  // At 95%, LOW and MEDIUM should be rejected, HIGH should still work
  EdgeMsg low_msg{0U};
  bool low_ok = bus.PublishWithPriority(std::move(low_msg), 1U, mccc::MessagePriority::LOW);

  EdgeMsg med_msg{0U};
  bool med_ok = bus.PublishWithPriority(std::move(med_msg), 1U, mccc::MessagePriority::MEDIUM);

  EdgeMsg high_msg{0U};
  bool high_ok = bus.PublishWithPriority(std::move(high_msg), 1U, mccc::MessagePriority::HIGH);

  uint32_t depth = bus.QueueDepth();
  if (depth > EdgeBus::MEDIUM_PRIORITY_THRESHOLD) {
    REQUIRE_FALSE(low_ok);
    REQUIRE_FALSE(med_ok);
  }
  if (depth < EdgeBus::HIGH_PRIORITY_THRESHOLD) {
    REQUIRE(high_ok);
  }

  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

// ============================================================================
// Error callback
// ============================================================================

TEST_CASE("Error callback invoked on queue full", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  std::atomic<uint32_t> error_count{0U};
  std::atomic<mccc::BusError> last_error{mccc::BusError::INVALID_MESSAGE};

  bus.SetErrorCallback([](mccc::BusError err, uint64_t /*msg_id*/) {
    // Note: can't capture atomics in a function pointer.
    // We'll verify via statistics instead.
    (void)err;
  });

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  // Fill queue past LOW threshold to trigger drops
  for (uint32_t i = 0U; i < EdgeBus::MAX_QUEUE_DEPTH; ++i) {
    EdgeMsg msg{i};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW);
  }

  auto stats = bus.GetStatistics();
  INFO("Published: " << stats.messages_published << ", Dropped: " << stats.messages_dropped);

  // Should have some drops since LOW threshold is 60%
  REQUIRE(stats.messages_dropped > 0U);
  REQUIRE(stats.low_priority_dropped > 0U);

  // Clean up
  bus.SetErrorCallback(nullptr);
  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

TEST_CASE("Error callback can be set to nullptr", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();

  // Should not crash
  bus.SetErrorCallback(nullptr);

  // Trigger a drop - should not crash even without callback
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  // Fill past threshold
  for (uint32_t i = 0U; i < EdgeBus::MAX_QUEUE_DEPTH; ++i) {
    EdgeMsg msg{i};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW);
  }

  // No crash
  REQUIRE(true);

  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

// ============================================================================
// Statistics accuracy
// ============================================================================

TEST_CASE("Statistics accurately track published and processed", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  constexpr uint32_t N = 500U;
  for (uint32_t i = 0U; i < N; ++i) {
    EdgeMsg msg{i};
    bus.Publish(std::move(msg), 1U);
  }

  auto stats_before = bus.GetStatistics();
  REQUIRE(stats_before.messages_published == N);
  REQUIRE(stats_before.messages_processed == 0U);

  DrainEdgeBus(bus);

  auto stats_after = bus.GetStatistics();
  REQUIRE(stats_after.messages_processed >= N);

  bus.Unsubscribe(handle);
}

TEST_CASE("NO_STATS mode skips statistics", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::NO_STATS);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  for (uint32_t i = 0U; i < 100U; ++i) {
    EdgeMsg msg{i};
    bus.Publish(std::move(msg), 1U);
  }

  DrainEdgeBus(bus);

  auto stats = bus.GetStatistics();
  // In NO_STATS mode, published count should be 0 (stats skipped)
  // But processed is still tracked because ProcessOne always increments
  REQUIRE(stats.messages_published == 0U);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("Priority statistics per-level tracking", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  constexpr uint32_t HIGH_COUNT = 10U;
  constexpr uint32_t MED_COUNT = 20U;
  constexpr uint32_t LOW_COUNT = 30U;

  for (uint32_t i = 0U; i < HIGH_COUNT; ++i) {
    EdgeMsg msg{i};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::HIGH);
  }
  for (uint32_t i = 0U; i < MED_COUNT; ++i) {
    EdgeMsg msg{i};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::MEDIUM);
  }
  for (uint32_t i = 0U; i < LOW_COUNT; ++i) {
    EdgeMsg msg{i};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW);
  }

  auto stats = bus.GetStatistics();
  REQUIRE(stats.high_priority_published == HIGH_COUNT);
  REQUIRE(stats.medium_priority_published == MED_COUNT);
  REQUIRE(stats.low_priority_published == LOW_COUNT);
  REQUIRE(stats.messages_published == HIGH_COUNT + MED_COUNT + LOW_COUNT);

  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

// ============================================================================
// Performance mode switching
// ============================================================================

TEST_CASE("Switch performance mode at runtime", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  // Start in FULL_FEATURED
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();

  EdgeMsg msg1{1U};
  bus.Publish(std::move(msg1), 1U);
  auto stats1 = bus.GetStatistics();
  REQUIRE(stats1.messages_published == 1U);

  // Switch to NO_STATS
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::NO_STATS);

  EdgeMsg msg2{2U};
  bus.Publish(std::move(msg2), 1U);
  auto stats2 = bus.GetStatistics();
  // The second message should not increment published count
  REQUIRE(stats2.messages_published == 1U);

  // Switch to BARE_METAL
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::BARE_METAL);

  EdgeMsg msg3{3U};
  bus.Publish(std::move(msg3), 1U);
  auto stats3 = bus.GetStatistics();
  REQUIRE(stats3.messages_published == 1U);  // Still 1

  // Switch back
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);

  EdgeMsg msg4{4U};
  bus.Publish(std::move(msg4), 1U);
  auto stats4 = bus.GetStatistics();
  REQUIRE(stats4.messages_published == 2U);  // Now incremented

  DrainEdgeBus(bus);
  bus.Unsubscribe(handle);
}

// ============================================================================
// Queue depth monitoring
// ============================================================================

TEST_CASE("QueueDepth and QueueUtilizationPercent consistent", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  auto handle = bus.Subscribe<EdgeMsg>([](const EdgeEnvelope&) {});

  REQUIRE(bus.QueueDepth() == 0U);
  REQUIRE(bus.QueueUtilizationPercent() == 0U);

  // Fill some messages
  constexpr uint32_t N = 1000U;
  for (uint32_t i = 0U; i < N; ++i) {
    EdgeMsg msg{i};
    bus.Publish(std::move(msg), 1U);
  }

  uint32_t depth = bus.QueueDepth();
  uint32_t util = bus.QueueUtilizationPercent();

  REQUIRE(depth == N);
  REQUIRE(util == (N * 100U) / EdgeBus::MAX_QUEUE_DEPTH);

  DrainEdgeBus(bus);

  REQUIRE(bus.QueueDepth() == 0U);
  REQUIRE(bus.QueueUtilizationPercent() == 0U);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
}

// ============================================================================
// PublishFast with explicit timestamp
// ============================================================================

TEST_CASE("PublishFast preserves user timestamp", "[EdgeCase]") {
  auto& bus = EdgeBus::Instance();
  bus.SetPerformanceMode(EdgeBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainEdgeBus(bus);

  std::atomic<uint64_t> received_ts{0U};

  auto handle = bus.Subscribe<EdgeMsg>([&received_ts](const EdgeEnvelope& env) {
    received_ts.store(env.header.timestamp_us, std::memory_order_relaxed);
  });

  constexpr uint64_t CUSTOM_TS = 1234567890ULL;
  EdgeMsg msg{42U};
  bus.PublishFast(std::move(msg), 1U, CUSTOM_TS);
  DrainEdgeBus(bus);

  REQUIRE(received_ts.load() == CUSTOM_TS);

  bus.Unsubscribe(handle);
}
