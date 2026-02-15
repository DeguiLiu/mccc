#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>
#include <thread>
#include <variant>

namespace {

struct MsgA {
  int value{0};
};
struct MsgB {
  float data{0.0f};
};
struct MsgC {
  uint32_t id{0};
};

using TestPayload = std::variant<MsgA, MsgB, MsgC>;
using TestBus = mccc::AsyncBus<TestPayload>;

}  // namespace

TEST_CASE("ProcessBatchWith dispatches all types", "[Visitor]") {
  auto& bus = TestBus::Instance();

  int a_count = 0;
  int b_count = 0;
  int c_count = 0;
  int a_sum = 0;

  auto visitor = mccc::make_overloaded(
      [&](const MsgA& m) {
        ++a_count;
        a_sum += m.value;
      },
      [&](const MsgB& m) {
        ++b_count;
        (void)m;
      },
      [&](const MsgC& m) {
        ++c_count;
        (void)m;
      });

  // Publish messages
  bus.Publish(MsgA{10}, 1);
  bus.Publish(MsgB{3.14f}, 1);
  bus.Publish(MsgA{20}, 1);
  bus.Publish(MsgC{99}, 1);

  // Process with visitor
  uint32_t processed = bus.ProcessBatchWith(visitor);

  REQUIRE(processed == 4);
  REQUIRE(a_count == 2);
  REQUIRE(b_count == 1);
  REQUIRE(c_count == 1);
  REQUIRE(a_sum == 30);
}

TEST_CASE("ProcessBatchWith returns 0 on empty queue", "[Visitor]") {
  auto& bus = TestBus::Instance();

  // Drain any remaining messages first
  auto drain = mccc::make_overloaded([](const MsgA&) {}, [](const MsgB&) {}, [](const MsgC&) {});
  while (bus.ProcessBatchWith(drain) > 0) {}

  uint32_t processed = bus.ProcessBatchWith(drain);
  REQUIRE(processed == 0);
}

TEST_CASE("ProcessBatchWith throughput vs ProcessBatch", "[Visitor][benchmark]") {
  auto& bus = TestBus::Instance();

  // Drain first
  auto drain = mccc::make_overloaded([](const MsgA&) {}, [](const MsgB&) {}, [](const MsgC&) {});
  while (bus.ProcessBatchWith(drain) > 0) {}

  // N must fit within queue depth (no consumer to drain during publish)
  constexpr uint32_t N = (TestBus::MAX_QUEUE_DEPTH > 2000U) ? (TestBus::MAX_QUEUE_DEPTH / 2U) : 1000U;
  std::atomic<uint32_t> visitor_count{0};
  std::atomic<uint32_t> callback_count{0};

  // -- Test 1: ProcessBatchWith (visitor) --
  uint32_t published1 = 0U;
  for (uint32_t i = 0; i < N; ++i) {
    if (bus.Publish(MsgA{static_cast<int>(i)}, 1)) {
      ++published1;
    }
  }
  REQUIRE(published1 > 0U);

  auto count_visitor =
      mccc::make_overloaded([&visitor_count](const MsgA&) { visitor_count.fetch_add(1U, std::memory_order_relaxed); },
                            [](const MsgB&) {}, [](const MsgC&) {});

  auto t1_start = std::chrono::steady_clock::now();
  while (visitor_count.load(std::memory_order_relaxed) < published1) {
    bus.ProcessBatchWith(count_visitor);
  }
  auto t1_end = std::chrono::steady_clock::now();
  auto visitor_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1_end - t1_start).count();

  // -- Test 2: ProcessBatch (callback table) --
  auto handle = bus.Subscribe<MsgA>([&callback_count](const mccc::MessageEnvelope<TestPayload>&) {
    callback_count.fetch_add(1U, std::memory_order_relaxed);
  });

  uint32_t published2 = 0U;
  for (uint32_t i = 0; i < N; ++i) {
    if (bus.Publish(MsgA{static_cast<int>(i)}, 1)) {
      ++published2;
    }
  }
  REQUIRE(published2 > 0U);

  auto t2_start = std::chrono::steady_clock::now();
  while (callback_count.load(std::memory_order_relaxed) < published2) {
    bus.ProcessBatch();
  }
  auto t2_end = std::chrono::steady_clock::now();
  auto callback_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2_end - t2_start).count();

  bus.Unsubscribe(handle);

  // Visitor should be faster (no lock, no callback table lookup)
  // Just report, don't hard-assert ratio (depends on platform)
  WARN("Visitor: " << visitor_ns / published1 << " ns/msg, Callback: " << callback_ns / published2 << " ns/msg");
  REQUIRE(visitor_count.load() == published1);
  REQUIRE(callback_count.load() == published2);
}
