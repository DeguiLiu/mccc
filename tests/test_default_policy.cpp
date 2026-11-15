/**
 * @file test_default_policy.cpp
 * @brief Verify that unsafe performance modes are rejected by default.
 */

#include <cstdint>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

namespace {

struct PolicyMsg {
  uint32_t value;
};
using PolicyPayload = std::variant<PolicyMsg>;
using PolicyBus = mccc::AsyncBus<PolicyPayload>;

std::atomic<int> g_invalid_mode{0};
std::atomic<int> g_queue_full{0};

void PolicyErrorHandler(mccc::BusError error, uint64_t /*msg_id*/) noexcept {
  if (error == mccc::BusError::INVALID_MODE) {
    g_invalid_mode.fetch_add(1, std::memory_order_relaxed);
  } else if (error == mccc::BusError::QUEUE_FULL) {
    g_queue_full.fetch_add(1, std::memory_order_relaxed);
  }
}

void DrainBus(PolicyBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

}  // namespace

TEST_CASE("Unsafe performance modes require an explicit opt-in", "[Policy]") {
  g_invalid_mode.store(0, std::memory_order_relaxed);
  g_queue_full.store(0, std::memory_order_relaxed);

  auto& bus = PolicyBus::Instance();
  bus.SetErrorCallback(&PolicyErrorHandler);
  DrainBus(bus);

  bus.SetPerformanceMode(PolicyBus::PerformanceMode::BARE_METAL);
  REQUIRE(g_invalid_mode.load(std::memory_order_relaxed) == 1);

  // FULL_FEATURED admission is still active: MEDIUM messages are rejected once
  // the queue reaches its 80% threshold.
  uint32_t published = 0U;
  for (uint32_t i = 0U; i < PolicyBus::MAX_QUEUE_DEPTH; ++i) {
    if (!bus.Publish(PolicyPayload{PolicyMsg{i}}, 1U)) {
      break;
    }
    ++published;
  }
  REQUIRE(published > 0U);
  REQUIRE(published < PolicyBus::MAX_QUEUE_DEPTH);
  REQUIRE(g_queue_full.load(std::memory_order_relaxed) > 0);

  DrainBus(bus);
}
