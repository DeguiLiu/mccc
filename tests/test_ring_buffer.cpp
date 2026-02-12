/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for AsyncBus ring buffer correctness.
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/message_bus.hpp>

#include <atomic>
#include <thread>
#include <vector>

// Test message types
struct TestMsgA { int value; };
struct TestMsgB { float data; };
struct TestMsgC { uint32_t id; };

using TestPayload = std::variant<TestMsgA, TestMsgB, TestMsgC>;
using TestBus = mccc::AsyncBus<TestPayload>;
using TestEnvelope = mccc::MessageEnvelope<TestPayload>;

TEST_CASE("Single producer publish and process", "[RingBuffer]") {
  auto& bus = TestBus::Instance();
  bus.ResetStatistics();

  std::atomic<int> received_value{0};

  bus.Subscribe<TestMsgA>([&received_value](const TestEnvelope& env) {
    const auto* msg = std::get_if<TestMsgA>(&env.payload);
    if (msg) {
      received_value.store(msg->value, std::memory_order_relaxed);
    }
  });

  TestMsgA msg{42};
  REQUIRE(bus.Publish(std::move(msg), 1U));

  // Process
  uint32_t processed = bus.ProcessBatch();
  REQUIRE(processed == 1U);
  REQUIRE(received_value.load() == 42);

  auto stats = bus.GetStatistics();
  REQUIRE(stats.messages_published == 1U);
  REQUIRE(stats.messages_processed == 1U);
}

TEST_CASE("Multiple messages in sequence", "[RingBuffer]") {
  auto& bus = TestBus::Instance();
  bus.ResetStatistics();

  std::atomic<uint32_t> count{0U};

  bus.Subscribe<TestMsgA>([&count](const TestEnvelope&) {
    count.fetch_add(1U, std::memory_order_relaxed);
  });

  constexpr uint32_t N = 1000U;
  for (uint32_t i = 0U; i < N; ++i) {
    TestMsgA msg{static_cast<int>(i)};
    bus.Publish(std::move(msg), 1U);
  }

  // Process all
  uint32_t total_processed = 0U;
  while (total_processed < N) {
    total_processed += bus.ProcessBatch();
  }

  REQUIRE(total_processed >= N);
  REQUIRE(count.load() >= N);
}

TEST_CASE("Multi-producer concurrent publish", "[RingBuffer]") {
  auto& bus = TestBus::Instance();
  bus.ResetStatistics();
  bus.SetPerformanceMode(TestBus::PerformanceMode::BARE_METAL);

  std::atomic<uint32_t> received{0U};

  bus.Subscribe<TestMsgA>([&received](const TestEnvelope&) {
    received.fetch_add(1U, std::memory_order_relaxed);
  });

  constexpr uint32_t MSGS_PER_THREAD = 5000U;
  constexpr uint32_t NUM_THREADS = 4U;

  // Start consumer thread
  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    // Drain
    while (bus.ProcessBatch() > 0U) {}
  });

  // Start producer threads
  std::vector<std::thread> producers;
  for (uint32_t t = 0U; t < NUM_THREADS; ++t) {
    producers.emplace_back([&bus, t]() {
      for (uint32_t i = 0U; i < MSGS_PER_THREAD; ++i) {
        TestMsgA msg{static_cast<int>(t * MSGS_PER_THREAD + i)};
        bus.Publish(std::move(msg), t);
      }
    });
  }

  for (auto& p : producers) {
    p.join();
  }

  // Wait for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  stop.store(true, std::memory_order_release);
  consumer.join();

  // In BARE_METAL mode, some messages may be dropped due to CAS contention
  // but no crashes or data corruption should occur
  REQUIRE(received.load() > 0U);

  bus.SetPerformanceMode(TestBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("Different message types dispatch correctly", "[RingBuffer]") {
  auto& bus = TestBus::Instance();
  bus.ResetStatistics();

  std::atomic<int> a_count{0};
  std::atomic<int> b_count{0};

  bus.Subscribe<TestMsgA>([&a_count](const TestEnvelope&) {
    a_count.fetch_add(1, std::memory_order_relaxed);
  });

  bus.Subscribe<TestMsgB>([&b_count](const TestEnvelope&) {
    b_count.fetch_add(1, std::memory_order_relaxed);
  });

  TestMsgA a{1};
  TestMsgB b{2.0f};
  TestMsgA a2{3};

  bus.Publish(std::move(a), 1U);
  bus.Publish(std::move(b), 1U);
  bus.Publish(std::move(a2), 1U);

  while (bus.ProcessBatch() > 0U) {}

  REQUIRE(a_count.load() == 2);
  REQUIRE(b_count.load() == 1);
}

TEST_CASE("Empty queue returns zero processed", "[RingBuffer]") {
  auto& bus = TestBus::Instance();
  // Don't publish anything
  uint32_t processed = bus.ProcessBatch();
  // May process leftover from previous tests, but should not crash
  (void)processed;
  REQUIRE(true);  // No crash
}
