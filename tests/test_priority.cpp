/**
 * @file test_priority.cpp
 * @brief Unit tests for priority-based admission control.
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

#include <atomic>
#include <thread>

struct PrioMsg { int value; };
using PrioPayload = std::variant<PrioMsg>;
using PrioBus = mccc::AsyncBus<PrioPayload>;
using PrioEnvelope = mccc::MessageEnvelope<PrioPayload>;

static void DrainPrioBus(PrioBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

TEST_CASE("HIGH priority accepted at high queue depth", "[Priority]") {
  auto& bus = PrioBus::Instance();
  bus.SetPerformanceMode(PrioBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainPrioBus(bus);

  // Subscribe (needed for dispatch)
  auto handle = bus.Subscribe<PrioMsg>([](const PrioEnvelope&) {});

  // Fill queue to ~85% (above LOW threshold 60%, above MEDIUM threshold 80%)
  // Don't process - let queue fill up
  uint32_t target_depth = (PrioBus::MAX_QUEUE_DEPTH * 85U) / 100U;

  uint32_t filled = 0U;
  for (uint32_t i = 0U; i < target_depth + 10000U; ++i) {
    PrioMsg msg{static_cast<int>(i)};
    if (bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::HIGH)) {
      ++filled;
    }
  }

  // At 85% depth, LOW should be rejected, MEDIUM should be rejected, HIGH should still be accepted
  uint32_t depth = bus.QueueDepth();
  INFO("Queue depth: " << depth);

  // Try HIGH - should succeed (threshold is 99%)
  PrioMsg high_msg{999};
  bool high_accepted = bus.PublishWithPriority(std::move(high_msg), 1U, mccc::MessagePriority::HIGH);

  // Try LOW - should fail (threshold is 60%)
  PrioMsg low_msg{998};
  bool low_accepted = bus.PublishWithPriority(std::move(low_msg), 1U, mccc::MessagePriority::LOW);

  if (depth > PrioBus::LOW_PRIORITY_THRESHOLD) {
    REQUIRE_FALSE(low_accepted);
  }

  if (depth < PrioBus::HIGH_PRIORITY_THRESHOLD) {
    REQUIRE(high_accepted);
  }

  // Drain queue
  DrainPrioBus(bus);
  bus.Unsubscribe(handle);
}

TEST_CASE("Priority ordering: LOW dropped first", "[Priority]") {
  auto& bus = PrioBus::Instance();
  bus.SetPerformanceMode(PrioBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainPrioBus(bus);

  auto handle = bus.Subscribe<PrioMsg>([](const PrioEnvelope&) {});

  // Fill queue past LOW threshold (60%)
  uint32_t low_threshold = PrioBus::LOW_PRIORITY_THRESHOLD;
  for (uint32_t i = 0U; i < low_threshold + 5000U; ++i) {
    PrioMsg msg{static_cast<int>(i)};
    bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::HIGH);
  }

  // Now try LOW priority - should be rejected
  uint32_t low_rejected = 0U;
  uint32_t low_accepted = 0U;
  for (uint32_t i = 0U; i < 100U; ++i) {
    PrioMsg msg{static_cast<int>(i)};
    if (bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW)) {
      ++low_accepted;
    } else {
      ++low_rejected;
    }
  }

  auto stats = bus.GetStatistics();
  INFO("Queue depth: " << bus.QueueDepth());
  INFO("LOW rejected: " << low_rejected << ", accepted: " << low_accepted);

  // If queue is above LOW threshold, most LOW messages should be rejected
  if (bus.QueueDepth() > low_threshold) {
    REQUIRE(low_rejected > low_accepted);
  }

  // Drain
  DrainPrioBus(bus);
  bus.Unsubscribe(handle);
}

TEST_CASE("BARE_METAL mode bypasses priority check", "[Priority]") {
  auto& bus = PrioBus::Instance();
  bus.SetPerformanceMode(PrioBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainPrioBus(bus);

  auto handle = bus.Subscribe<PrioMsg>([](const PrioEnvelope&) {});

  // In BARE_METAL, priority check is skipped
  uint32_t accepted = 0U;
  for (uint32_t i = 0U; i < 1000U; ++i) {
    PrioMsg msg{static_cast<int>(i)};
    if (bus.PublishWithPriority(std::move(msg), 1U, mccc::MessagePriority::LOW)) {
      ++accepted;
    }
  }

  REQUIRE(accepted == 1000U);

  // Drain
  DrainPrioBus(bus);
  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(PrioBus::PerformanceMode::FULL_FEATURED);
}
