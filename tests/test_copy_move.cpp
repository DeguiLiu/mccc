/**
 * @file test_copy_move.cpp
 * @brief Verify copy/move semantics through the message bus pipeline.
 *
 * Inspired by eventpp's CopyMoveCounter pattern.
 * Tracks exactly how many copies and moves occur for each message
 * to ensure the bus doesn't introduce unnecessary overhead.
 */

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

// ============================================================================
// CopyMoveCounter - tracks copy/move operations
// ============================================================================

struct CopyMoveCounter {
  int value;
  int copy_count;
  int move_count;

  explicit CopyMoveCounter(int v = 0) noexcept : value(v), copy_count(0), move_count(0) {}

  CopyMoveCounter(const CopyMoveCounter& other) noexcept
      : value(other.value), copy_count(other.copy_count + 1), move_count(other.move_count) {}

  CopyMoveCounter& operator=(const CopyMoveCounter& other) noexcept {
    if (this != &other) {
      value = other.value;
      copy_count = other.copy_count + 1;
      move_count = other.move_count;
    }
    return *this;
  }

  CopyMoveCounter(CopyMoveCounter&& other) noexcept
      : value(other.value), copy_count(other.copy_count), move_count(other.move_count + 1) {}

  CopyMoveCounter& operator=(CopyMoveCounter&& other) noexcept {
    if (this != &other) {
      value = other.value;
      copy_count = other.copy_count;
      move_count = other.move_count + 1;
    }
    return *this;
  }
};

struct DummyMsg {
  int x;
};

using CmPayload = std::variant<CopyMoveCounter, DummyMsg>;
using CmBus = mccc::AsyncBus<CmPayload>;
using CmEnvelope = mccc::MessageEnvelope<CmPayload>;

static void DrainCmBus(CmBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

// ============================================================================
// Copy/Move tracking tests
// ============================================================================

TEST_CASE("Publish with std::move uses move semantics", "[CopyMove]") {
  auto& bus = CmBus::Instance();
  bus.SetPerformanceMode(CmBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainCmBus(bus);

  int received_copies = -1;
  int received_moves = -1;
  int received_value = -1;

  auto handle = bus.Subscribe<CopyMoveCounter>([&](const CmEnvelope& env) {
    const auto* msg = std::get_if<CopyMoveCounter>(&env.payload);
    if (msg != nullptr) {
      received_copies = msg->copy_count;
      received_moves = msg->move_count;
      received_value = msg->value;
    }
  });

  CopyMoveCounter msg(42);
  REQUIRE(msg.copy_count == 0);
  REQUIRE(msg.move_count == 0);

  bus.Publish(std::move(msg), 1U);
  DrainCmBus(bus);

  REQUIRE(received_value == 42);
  INFO("Copies: " << received_copies << ", Moves: " << received_moves);

  // Should have zero copies (all moves)
  REQUIRE(received_copies == 0);
  // Should have some moves (variant construction + ring buffer placement)
  REQUIRE(received_moves > 0);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(CmBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("Multiple publish - consistent copy/move counts", "[CopyMove]") {
  auto& bus = CmBus::Instance();
  bus.SetPerformanceMode(CmBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainCmBus(bus);

  int first_copies = -1;
  int first_moves = -1;
  bool first_recorded = false;
  bool all_consistent = true;

  auto handle = bus.Subscribe<CopyMoveCounter>([&](const CmEnvelope& env) {
    const auto* msg = std::get_if<CopyMoveCounter>(&env.payload);
    if (msg != nullptr) {
      if (!first_recorded) {
        first_copies = msg->copy_count;
        first_moves = msg->move_count;
        first_recorded = true;
      } else {
        if (msg->copy_count != first_copies || msg->move_count != first_moves) {
          all_consistent = false;
        }
      }
    }
  });

  constexpr uint32_t N = 100U;
  for (uint32_t i = 0U; i < N; ++i) {
    CopyMoveCounter msg(static_cast<int>(i));
    bus.Publish(std::move(msg), 1U);
  }

  DrainCmBus(bus);

  REQUIRE(first_recorded);
  INFO("First message: copies=" << first_copies << ", moves=" << first_moves);
  REQUIRE(all_consistent);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(CmBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("Dispatch to callback does not add extra copies", "[CopyMove]") {
  auto& bus = CmBus::Instance();
  bus.SetPerformanceMode(CmBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainCmBus(bus);

  int sub1_copies = -1;
  int sub2_copies = -1;

  auto h1 = bus.Subscribe<CopyMoveCounter>([&sub1_copies](const CmEnvelope& env) {
    const auto* msg = std::get_if<CopyMoveCounter>(&env.payload);
    if (msg != nullptr) {
      sub1_copies = msg->copy_count;
    }
  });

  auto h2 = bus.Subscribe<CopyMoveCounter>([&sub2_copies](const CmEnvelope& env) {
    const auto* msg = std::get_if<CopyMoveCounter>(&env.payload);
    if (msg != nullptr) {
      sub2_copies = msg->copy_count;
    }
  });

  CopyMoveCounter msg(99);
  bus.Publish(std::move(msg), 1U);
  DrainCmBus(bus);

  INFO("Subscriber 1 copies: " << sub1_copies);
  INFO("Subscriber 2 copies: " << sub2_copies);

  // Both subscribers receive const reference to the same envelope -
  // they should see the same copy count
  REQUIRE(sub1_copies == sub2_copies);
  // Should be zero copies (dispatch passes const ref)
  REQUIRE(sub1_copies == 0);

  bus.Unsubscribe(h1);
  bus.Unsubscribe(h2);
  bus.SetPerformanceMode(CmBus::PerformanceMode::FULL_FEATURED);
}

// ============================================================================
// Large payload move semantics
// ============================================================================

struct LargePayload {
  static std::atomic<int> copy_count;
  static std::atomic<int> move_count;

  uint8_t data[256];
  int id;

  explicit LargePayload(int i = 0) noexcept : id(i) {
    // Fill with pattern for integrity check
    for (size_t j = 0; j < sizeof(data); ++j) {
      data[j] = static_cast<uint8_t>(i + j);
    }
  }

  LargePayload(const LargePayload& other) noexcept : id(other.id) {
    std::memcpy(data, other.data, sizeof(data));
    copy_count.fetch_add(1, std::memory_order_relaxed);
  }

  LargePayload& operator=(const LargePayload& other) noexcept {
    if (this != &other) {
      id = other.id;
      std::memcpy(data, other.data, sizeof(data));
      copy_count.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  LargePayload(LargePayload&& other) noexcept : id(other.id) {
    std::memcpy(data, other.data, sizeof(data));
    move_count.fetch_add(1, std::memory_order_relaxed);
  }

  LargePayload& operator=(LargePayload&& other) noexcept {
    if (this != &other) {
      id = other.id;
      std::memcpy(data, other.data, sizeof(data));
      move_count.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  bool verify() const noexcept {
    for (size_t j = 0; j < sizeof(data); ++j) {
      if (data[j] != static_cast<uint8_t>(id + j)) {
        return false;
      }
    }
    return true;
  }
};

std::atomic<int> LargePayload::copy_count{0};
std::atomic<int> LargePayload::move_count{0};

using LargePayloadVariant = std::variant<LargePayload, DummyMsg>;
using LargeBus = mccc::AsyncBus<LargePayloadVariant>;
using LargeEnvelope = mccc::MessageEnvelope<LargePayloadVariant>;

TEST_CASE("Large payload data integrity through bus", "[CopyMove]") {
  auto& bus = LargeBus::Instance();
  bus.SetPerformanceMode(LargeBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  while (bus.ProcessBatch() > 0U) {}

  LargePayload::copy_count.store(0);
  LargePayload::move_count.store(0);

  bool integrity_ok = false;
  int received_id = -1;

  auto handle = bus.Subscribe<LargePayload>([&](const LargeEnvelope& env) {
    const auto* msg = std::get_if<LargePayload>(&env.payload);
    if (msg != nullptr) {
      integrity_ok = msg->verify();
      received_id = msg->id;
    }
  });

  LargePayload msg(77);
  bus.Publish(std::move(msg), 1U);
  while (bus.ProcessBatch() > 0U) {}

  REQUIRE(received_id == 77);
  REQUIRE(integrity_ok);

  INFO("Large payload copies: " << LargePayload::copy_count.load());
  INFO("Large payload moves: " << LargePayload::move_count.load());

  // Should use moves, not copies
  REQUIRE(LargePayload::copy_count.load() == 0);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(LargeBus::PerformanceMode::FULL_FEATURED);
}

// ============================================================================
// FixedVector copy/move tests
// ============================================================================

TEST_CASE("FixedVector move constructor transfers elements", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 8> vec;
  vec.emplace_back(1);
  vec.emplace_back(2);
  vec.emplace_back(3);

  REQUIRE(vec.size() == 3U);
  int original_copies = vec[0].copy_count;

  // Move construct
  mccc::FixedVector<CopyMoveCounter, 8> moved(std::move(vec));

  REQUIRE(moved.size() == 3U);
  REQUIRE(moved[0].value == 1);
  REQUIRE(moved[1].value == 2);
  REQUIRE(moved[2].value == 3);

  // Should have used moves, not copies
  REQUIRE(moved[0].copy_count == original_copies);
  REQUIRE(moved[0].move_count > 0);
}

TEST_CASE("FixedVector copy constructor copies elements", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 8> vec;
  vec.emplace_back(10);
  vec.emplace_back(20);

  int moves_before = vec[0].move_count;

  // Copy construct
  mccc::FixedVector<CopyMoveCounter, 8> copied(vec);

  REQUIRE(copied.size() == 2U);
  REQUIRE(copied[0].value == 10);
  REQUIRE(copied[1].value == 20);

  // Should have one additional copy
  REQUIRE(copied[0].copy_count == vec[0].copy_count + 1);
  // Move count should not change during copy
  REQUIRE(copied[0].move_count == moves_before);
}

TEST_CASE("FixedVector push_back with move", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 4> vec;

  CopyMoveCounter item(42);
  REQUIRE(item.copy_count == 0);
  REQUIRE(item.move_count == 0);

  vec.push_back(std::move(item));

  REQUIRE(vec.size() == 1U);
  REQUIRE(vec[0].value == 42);
  // Should have used move, not copy
  REQUIRE(vec[0].copy_count == 0);
  REQUIRE(vec[0].move_count > 0);
}

TEST_CASE("FixedVector push_back with copy", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 4> vec;

  CopyMoveCounter item(42);
  vec.push_back(item);  // lvalue - should copy

  REQUIRE(vec.size() == 1U);
  REQUIRE(vec[0].value == 42);
  REQUIRE(vec[0].copy_count > 0);
}

TEST_CASE("FixedVector full returns false", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 2> vec;

  REQUIRE(vec.push_back(CopyMoveCounter(1)));
  REQUIRE(vec.push_back(CopyMoveCounter(2)));
  REQUIRE(vec.full());
  REQUIRE_FALSE(vec.push_back(CopyMoveCounter(3)));

  REQUIRE(vec.size() == 2U);
}

TEST_CASE("FixedVector erase_unordered with move", "[CopyMove]") {
  mccc::FixedVector<CopyMoveCounter, 4> vec;
  vec.emplace_back(1);
  vec.emplace_back(2);
  vec.emplace_back(3);

  // Erase index 0 - should move last element to index 0
  vec.erase_unordered(0U);

  REQUIRE(vec.size() == 2U);
  // Element at [0] should now be the old [2] (value 3), moved
  REQUIRE(vec[0].value == 3);
  REQUIRE(vec[0].move_count > 0);
  REQUIRE(vec[1].value == 2);
}
