/**
 * @file test_backpressure.cpp
 * @brief Unit tests for backpressure level monitoring.
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

struct BpMsg { int value; };
using BpPayload = std::variant<BpMsg>;
using BpBus = mccc::AsyncBus<BpPayload>;
using BpEnvelope = mccc::MessageEnvelope<BpPayload>;

static void DrainBpBus(BpBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

TEST_CASE("Backpressure NORMAL when queue empty", "[Backpressure]") {
  auto& bus = BpBus::Instance();
  // Drain first
  DrainBpBus(bus);

  auto level = bus.GetBackpressureLevel();
  REQUIRE(level == mccc::BackpressureLevel::NORMAL);
  REQUIRE(bus.QueueUtilizationPercent() < 75U);
}

TEST_CASE("Backpressure level transitions", "[Backpressure]") {
  auto& bus = BpBus::Instance();
  bus.SetPerformanceMode(BpBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();

  // Drain first
  DrainBpBus(bus);

  auto handle = bus.Subscribe<BpMsg>([](const BpEnvelope&) {});

  // Fill to ~76% (WARNING threshold is 75%)
  uint32_t warning_target = (BpBus::MAX_QUEUE_DEPTH * 76U) / 100U;
  for (uint32_t i = 0U; i < warning_target; ++i) {
    BpMsg msg{static_cast<int>(i)};
    bus.Publish(std::move(msg), 1U);
  }

  auto level = bus.GetBackpressureLevel();
  INFO("Queue depth: " << bus.QueueDepth() << " / " << BpBus::MAX_QUEUE_DEPTH);
  // Should be at least WARNING
  REQUIRE(static_cast<uint8_t>(level) >= static_cast<uint8_t>(mccc::BackpressureLevel::WARNING));

  // Fill to ~91% (CRITICAL threshold is 90%)
  uint32_t critical_target = (BpBus::MAX_QUEUE_DEPTH * 91U) / 100U;
  for (uint32_t i = warning_target; i < critical_target; ++i) {
    BpMsg msg{static_cast<int>(i)};
    bus.Publish(std::move(msg), 1U);
  }

  level = bus.GetBackpressureLevel();
  INFO("Queue depth after critical fill: " << bus.QueueDepth());
  REQUIRE(static_cast<uint8_t>(level) >= static_cast<uint8_t>(mccc::BackpressureLevel::CRITICAL));

  // Drain
  DrainBpBus(bus);

  level = bus.GetBackpressureLevel();
  REQUIRE(level == mccc::BackpressureLevel::NORMAL);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(BpBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("Statistics counting", "[Backpressure]") {
  auto& bus = BpBus::Instance();
  bus.SetPerformanceMode(BpBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();

  // Drain first
  DrainBpBus(bus);

  auto handle = bus.Subscribe<BpMsg>([](const BpEnvelope&) {});

  // Publish some messages
  for (uint32_t i = 0U; i < 100U; ++i) {
    BpMsg msg{static_cast<int>(i)};
    bus.Publish(std::move(msg), 1U);
  }

  auto stats = bus.GetStatistics();
  REQUIRE(stats.messages_published == 100U);

  // Process them
  DrainBpBus(bus);

  stats = bus.GetStatistics();
  REQUIRE(stats.messages_processed >= 100U);

  // Reset
  bus.ResetStatistics();
  stats = bus.GetStatistics();
  REQUIRE(stats.messages_published == 0U);
  REQUIRE(stats.messages_processed == 0U);
  REQUIRE(stats.messages_dropped == 0U);

  bus.Unsubscribe(handle);
}

TEST_CASE("Priority statistics tracking", "[Backpressure]") {
  auto& bus = BpBus::Instance();
  bus.SetPerformanceMode(BpBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();

  // Drain first
  DrainBpBus(bus);

  auto handle = bus.Subscribe<BpMsg>([](const BpEnvelope&) {});

  BpMsg h{1};
  bus.PublishWithPriority(std::move(h), 1U, mccc::MessagePriority::HIGH);
  BpMsg m{2};
  bus.PublishWithPriority(std::move(m), 1U, mccc::MessagePriority::MEDIUM);
  BpMsg l{3};
  bus.PublishWithPriority(std::move(l), 1U, mccc::MessagePriority::LOW);

  auto stats = bus.GetStatistics();
  REQUIRE(stats.high_priority_published == 1U);
  REQUIRE(stats.medium_priority_published == 1U);
  REQUIRE(stats.low_priority_published == 1U);

  // Drain
  DrainBpBus(bus);
  bus.Unsubscribe(handle);
}
