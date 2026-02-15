/**
 * @file test_multithread.cpp
 * @brief Multi-producer stress tests and concurrent subscribe/unsubscribe.
 *
 * Inspired by eventpp multithread test patterns:
 * - High thread count (up to 32 producers)
 * - Concurrent subscribe/unsubscribe during publish
 * - Data integrity verification (no lost or corrupted messages)
 */

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <mccc/mccc.hpp>
#include <numeric>
#include <thread>
#include <vector>

// ============================================================================
// Test types (unique to this file to avoid singleton conflicts)
// ============================================================================

struct MtMsg {
  uint32_t thread_id;
  uint32_t sequence;
  uint64_t checksum;  // thread_id ^ sequence for integrity check
};

struct MtMsgB {
  float value;
};

using MtPayload = std::variant<MtMsg, MtMsgB>;
using MtBus = mccc::AsyncBus<MtPayload>;
using MtEnvelope = mccc::MessageEnvelope<MtPayload>;

// Helper: drain all pending messages
static void DrainBus(MtBus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

// ============================================================================
// Multi-producer stress tests
// ============================================================================

TEST_CASE("4 producers, 1 consumer - data integrity", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainBus(bus);

  constexpr uint32_t NUM_THREADS = 4U;
  constexpr uint32_t MSGS_PER_THREAD = 10000U;

  std::atomic<uint32_t> received{0U};
  std::atomic<uint32_t> corrupted{0U};

  auto handle = bus.Subscribe<MtMsg>([&](const MtEnvelope& env) {
    const auto* msg = std::get_if<MtMsg>(&env.payload);
    if (msg != nullptr) {
      received.fetch_add(1U, std::memory_order_relaxed);
      // Verify data integrity
      if ((msg->thread_id ^ msg->sequence) != msg->checksum) {
        corrupted.fetch_add(1U, std::memory_order_relaxed);
      }
    }
  });

  // Consumer thread
  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    // Final drain
    for (int i = 0; i < 10; ++i) {
      if (bus.ProcessBatch() == 0U)
        break;
    }
  });

  // Producer threads
  std::vector<std::thread> producers;
  std::atomic<uint32_t> total_published{0U};

  for (uint32_t t = 0U; t < NUM_THREADS; ++t) {
    producers.emplace_back([&bus, &total_published, t, MSGS_PER_THREAD]() {
      for (uint32_t i = 0U; i < MSGS_PER_THREAD; ++i) {
        MtMsg msg{t, i, t ^ i};
        if (bus.Publish(std::move(msg), t)) {
          total_published.fetch_add(1U, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& p : producers) {
    p.join();
  }

  // Give consumer time to finish
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true, std::memory_order_release);
  consumer.join();

  INFO("Published: " << total_published.load());
  INFO("Received: " << received.load());
  INFO("Corrupted: " << corrupted.load());

  // Zero data corruption
  REQUIRE(corrupted.load() == 0U);
  // All received messages should have been published
  REQUIRE(received.load() <= total_published.load());
  // Should have received a significant portion (BARE_METAL may drop under contention)
  REQUIRE(received.load() > 0U);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("16 producers stress test - no crash", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainBus(bus);

  constexpr uint32_t NUM_THREADS = 16U;
  constexpr uint32_t MSGS_PER_THREAD = 5000U;

  std::atomic<uint32_t> received{0U};

  auto handle =
      bus.Subscribe<MtMsg>([&received](const MtEnvelope&) { received.fetch_add(1U, std::memory_order_relaxed); });

  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    for (int i = 0; i < 20; ++i) {
      if (bus.ProcessBatch() == 0U)
        break;
    }
  });

  std::vector<std::thread> producers;
  for (uint32_t t = 0U; t < NUM_THREADS; ++t) {
    producers.emplace_back([&bus, t, MSGS_PER_THREAD]() {
      for (uint32_t i = 0U; i < MSGS_PER_THREAD; ++i) {
        MtMsg msg{t, i, t ^ i};
        bus.Publish(std::move(msg), t);
      }
    });
  }

  for (auto& p : producers) {
    p.join();
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_release);
  consumer.join();

  INFO("Received: " << received.load() << " / " << NUM_THREADS * MSGS_PER_THREAD);
  REQUIRE(received.load() > 0U);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
}

TEST_CASE("32 producers burst - CAS contention", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::BARE_METAL);
  bus.ResetStatistics();
  DrainBus(bus);

  constexpr uint32_t NUM_THREADS = 32U;
  constexpr uint32_t MSGS_PER_THREAD = 1000U;

  std::atomic<uint32_t> published{0U};
  std::atomic<uint32_t> failed{0U};

  auto handle = bus.Subscribe<MtMsg>([](const MtEnvelope&) {});

  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    for (int i = 0; i < 20; ++i) {
      if (bus.ProcessBatch() == 0U)
        break;
    }
  });

  // Barrier: all threads start simultaneously for maximum contention
  std::atomic<uint32_t> ready{0U};
  std::vector<std::thread> producers;

  for (uint32_t t = 0U; t < NUM_THREADS; ++t) {
    producers.emplace_back([&, t, MSGS_PER_THREAD]() {
      ready.fetch_add(1U, std::memory_order_release);
      while (ready.load(std::memory_order_acquire) < NUM_THREADS) {
        std::this_thread::yield();
      }

      for (uint32_t i = 0U; i < MSGS_PER_THREAD; ++i) {
        MtMsg msg{t, i, t ^ i};
        if (bus.Publish(std::move(msg), t)) {
          published.fetch_add(1U, std::memory_order_relaxed);
        } else {
          failed.fetch_add(1U, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& p : producers) {
    p.join();
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_release);
  consumer.join();

  INFO("Published: " << published.load() << ", Failed: " << failed.load());
  // Under heavy CAS contention, some failures are expected
  REQUIRE(published.load() + failed.load() == NUM_THREADS * MSGS_PER_THREAD);
  REQUIRE(published.load() > 0U);

  bus.Unsubscribe(handle);
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
}

// ============================================================================
// Concurrent subscribe/unsubscribe
// ============================================================================

TEST_CASE("Concurrent subscribe while publishing", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainBus(bus);

  std::atomic<bool> stop{false};
  std::atomic<uint32_t> subscribe_count{0U};

  // Consumer thread
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
    }
    for (int i = 0; i < 10; ++i) {
      if (bus.ProcessBatch() == 0U)
        break;
    }
  });

  // Publisher thread
  std::thread publisher([&bus, &stop]() {
    uint32_t seq = 0U;
    while (!stop.load(std::memory_order_acquire)) {
      MtMsg msg{0U, seq++, 0U ^ seq};
      bus.Publish(std::move(msg), 0U);
    }
  });

  // Subscribe/unsubscribe thread - rapidly add and remove subscribers
  std::thread subscriber([&bus, &stop, &subscribe_count]() {
    while (!stop.load(std::memory_order_acquire)) {
      auto handle = bus.Subscribe<MtMsg>([](const MtEnvelope&) {});
      subscribe_count.fetch_add(1U, std::memory_order_relaxed);
      std::this_thread::yield();
      bus.Unsubscribe(handle);
    }
  });

  // Run for 500ms
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  stop.store(true, std::memory_order_release);

  publisher.join();
  subscriber.join();
  consumer.join();

  INFO("Subscribe/unsubscribe cycles: " << subscribe_count.load());
  REQUIRE(subscribe_count.load() > 0U);
}

TEST_CASE("Multiple subscribers concurrent unsubscribe", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainBus(bus);

  constexpr uint32_t NUM_SUBS = 8U;
  std::vector<mccc::SubscriptionHandle> handles;

  for (uint32_t i = 0U; i < NUM_SUBS; ++i) {
    handles.push_back(bus.Subscribe<MtMsg>([](const MtEnvelope&) {}));
  }

  // Unsubscribe from multiple threads simultaneously
  std::vector<std::thread> threads;
  std::atomic<uint32_t> success_count{0U};

  for (uint32_t i = 0U; i < NUM_SUBS; ++i) {
    threads.emplace_back([&bus, &handles, &success_count, i]() {
      if (bus.Unsubscribe(handles[i])) {
        success_count.fetch_add(1U, std::memory_order_relaxed);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(success_count.load() == NUM_SUBS);
}

// ============================================================================
// Producer-consumer balance test
// ============================================================================

TEST_CASE("Producer-consumer message count consistency", "[Multithread]") {
  auto& bus = MtBus::Instance();
  bus.SetPerformanceMode(MtBus::PerformanceMode::FULL_FEATURED);
  bus.ResetStatistics();
  DrainBus(bus);

  constexpr uint32_t TOTAL_MSGS = 50000U;

  std::atomic<uint32_t> produced{0U};
  std::atomic<uint32_t> consumed{0U};

  auto handle =
      bus.Subscribe<MtMsg>([&consumed](const MtEnvelope&) { consumed.fetch_add(1U, std::memory_order_relaxed); });

  // Consumer thread
  std::atomic<bool> stop{false};
  std::thread consumer([&bus, &stop]() {
    while (!stop.load(std::memory_order_acquire)) {
      bus.ProcessBatch();
      std::this_thread::yield();
    }
    // Thorough drain
    uint32_t drained;
    do {
      drained = bus.ProcessBatch();
    } while (drained > 0U);
  });

  // Single producer - controlled rate
  for (uint32_t i = 0U; i < TOTAL_MSGS; ++i) {
    MtMsg msg{0U, i, i};
    if (bus.Publish(std::move(msg), 0U)) {
      produced.fetch_add(1U, std::memory_order_relaxed);
    }
  }

  // Wait for consumer to catch up
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  stop.store(true, std::memory_order_release);
  consumer.join();

  auto stats = bus.GetStatistics();
  INFO("Produced: " << produced.load());
  INFO("Consumed: " << consumed.load());
  INFO("Stats published: " << stats.messages_published);
  INFO("Stats processed: " << stats.messages_processed);
  INFO("Stats dropped: " << stats.messages_dropped);

  // In FULL_FEATURED mode with single producer, should have zero drops
  // unless queue fills up (unlikely with 50K messages and active consumer)
  REQUIRE(consumed.load() == produced.load());
  REQUIRE(stats.messages_published == produced.load());

  bus.Unsubscribe(handle);
}
