/**
 * @file priority_demo.cpp
 * @brief Priority-based message system demo and stress test.
 *
 * Demonstrates intelligent message dropping based on priority levels.
 * Supports both normal demo mode and extreme stress test mode.
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#include "example_types.hpp"
#include "log_macro.hpp"

#include <cstring>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mccc/component.hpp>
#include <thread>

using namespace example;
using namespace mccc;

std::atomic<bool> running{true};

class StatisticsMonitor : public ExampleComponent {
 public:
  static std::shared_ptr<StatisticsMonitor> create() noexcept {
    std::shared_ptr<StatisticsMonitor> ptr(new StatisticsMonitor());
    ptr->InitializeComponent();
    return ptr;
  }

  void display_statistics(bool stress_mode = false) const noexcept {
    BusStatisticsSnapshot stats = ExampleBus::Instance().GetStatistics();

    if (stress_mode) {
      LOG_INFO("========== Stress Test Results ==========");
    } else {
      LOG_INFO("========== Message Bus Statistics ==========");
    }

    LOG_INFO("Total Published:  %lu", stats.messages_published);
    LOG_INFO("Total Processed:  %lu", stats.messages_processed);
    LOG_INFO("Total Dropped:    %lu", stats.messages_dropped);

    if (stress_mode) {
      LOG_INFO("--- Priority Breakdown ---");
      LOG_INFO("HIGH:   Published=%8lu  Dropped=%8lu  Rate=%.4f%%", stats.high_priority_published,
               stats.high_priority_dropped,
               calculate_drop_rate(stats.high_priority_published, stats.high_priority_dropped));
      LOG_INFO("MEDIUM: Published=%8lu  Dropped=%8lu  Rate=%.4f%%", stats.medium_priority_published,
               stats.medium_priority_dropped,
               calculate_drop_rate(stats.medium_priority_published, stats.medium_priority_dropped));
      LOG_INFO("LOW:    Published=%8lu  Dropped=%8lu  Rate=%.4f%%", stats.low_priority_published,
               stats.low_priority_dropped,
               calculate_drop_rate(stats.low_priority_published, stats.low_priority_dropped));
      LOG_INFO("==========================================");

      if (stats.high_priority_dropped == 0U) {
        LOG_INFO("SUCCESS: HIGH priority achieved ZERO message loss!");
      } else {
        LOG_ERROR("FAILURE: HIGH priority lost %lu messages", stats.high_priority_dropped);
      }
    } else {
      LOG_INFO("--- Priority Breakdown (Published) ---");
      LOG_INFO("HIGH:   %8lu", stats.high_priority_published);
      LOG_INFO("MEDIUM: %8lu", stats.medium_priority_published);
      LOG_INFO("LOW:    %8lu", stats.low_priority_published);
      LOG_INFO("--- Priority Breakdown (Dropped) ---");
      LOG_INFO("HIGH:   %8lu (%.2f%%)", stats.high_priority_dropped,
               calculate_drop_rate(stats.high_priority_published, stats.high_priority_dropped));
      LOG_INFO("MEDIUM: %8lu (%.2f%%)", stats.medium_priority_dropped,
               calculate_drop_rate(stats.medium_priority_published, stats.medium_priority_dropped));
      LOG_INFO("LOW:    %8lu (%.2f%%)", stats.low_priority_dropped,
               calculate_drop_rate(stats.low_priority_published, stats.low_priority_dropped));
      LOG_INFO("==========================================");
    }
  }

 private:
  StatisticsMonitor() = default;

  double calculate_drop_rate(uint64_t published, uint64_t dropped) const noexcept {
    uint64_t total = published + dropped;
    if (total == 0U) {
      return 0.0;
    }
    return (static_cast<double>(dropped) / static_cast<double>(total)) * 100.0;
  }
};

class MessageProducer : public ExampleComponent {
 public:
  static std::shared_ptr<MessageProducer> create(uint32_t id) noexcept {
    std::shared_ptr<MessageProducer> ptr(new MessageProducer(id));
    ptr->InitializeComponent();
    return ptr;
  }

  void send_critical_message() noexcept {
    MotionData data{1.0f, 2.0f, 3.0f, 100.0f};
    ExampleBus::Instance().PublishWithPriority(std::move(data), producer_id_, MessagePriority::HIGH);
  }

  void send_normal_message() noexcept {
    CameraFrame frame{1920, 1080, "RGB"};
    ExampleBus::Instance().PublishWithPriority(std::move(frame), producer_id_, MessagePriority::MEDIUM);
  }

  void send_debug_message() noexcept {
    SystemLog log{1, "Debug information"};
    ExampleBus::Instance().PublishWithPriority(std::move(log), producer_id_, MessagePriority::LOW);
  }

 private:
  explicit MessageProducer(uint32_t id) : producer_id_(id) {}
  uint32_t producer_id_;
};

class MessageConsumer : public ExampleComponent {
 public:
  static std::shared_ptr<MessageConsumer> create() noexcept {
    std::shared_ptr<MessageConsumer> ptr(new MessageConsumer());
    ptr->init();
    return ptr;
  }

 private:
  MessageConsumer() = default;

  void init() noexcept {
    InitializeComponent();
    SubscribeSafe<MotionData>(
        [](std::shared_ptr<ExampleComponent>, const MotionData&, const MessageHeader&) noexcept {});
    SubscribeSafe<CameraFrame>(
        [](std::shared_ptr<ExampleComponent>, const CameraFrame&, const MessageHeader&) noexcept {});
    SubscribeSafe<SystemLog>([](std::shared_ptr<ExampleComponent>, const SystemLog&, const MessageHeader&) noexcept {});
  }
};

void worker_thread() noexcept {
  while (running.load(std::memory_order_relaxed)) {
    uint32_t processed = ExampleBus::Instance().ProcessBatch();
    if (processed == 0U) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
}

void run_stress_test() {
  LOG_INFO("=== MCCC Priority System Stress Test ===");
  LOG_INFO("Verifying: HIGH priority messages achieve ZERO loss");

  auto monitor = StatisticsMonitor::create();
  auto consumer = MessageConsumer::create();
  auto producer1 = MessageProducer::create(1U);
  auto producer2 = MessageProducer::create(2U);
  auto producer3 = MessageProducer::create(3U);

  std::thread worker(worker_thread);

  LOG_INFO("Sending 200,000 messages (10%% HIGH, 60%% MEDIUM, 30%% LOW)");

  for (int i = 0; i < 200000; ++i) {
    if (i % 10 == 0)
      producer1->send_critical_message();
    if (i % 10 < 6)
      producer2->send_normal_message();
    if (i % 10 < 3)
      producer3->send_debug_message();
    if (i % 100 == 0)
      std::this_thread::sleep_for(std::chrono::microseconds(1));
  }

  LOG_INFO("Messages sent. Waiting for processing...");
  std::this_thread::sleep_for(std::chrono::seconds(2));

  monitor->display_statistics(true);

  running.store(false, std::memory_order_relaxed);
  worker.join();
}

void run_demo_mode() {
  LOG_INFO("=== MCCC Priority-Based Message System Demo ===");

  auto monitor = StatisticsMonitor::create();
  auto consumer = MessageConsumer::create();
  auto producer1 = MessageProducer::create(1U);
  auto producer2 = MessageProducer::create(2U);
  auto producer3 = MessageProducer::create(3U);

  std::thread worker(worker_thread);

  LOG_INFO("Phase 1: Normal load");
  for (int i = 0; i < 10000; ++i) {
    if (i % 10 == 0)
      producer1->send_critical_message();
    if (i % 10 < 6)
      producer2->send_normal_message();
    if (i % 10 < 3)
      producer3->send_debug_message();
    if (i % 20 == 0)
      std::this_thread::sleep_for(std::chrono::microseconds(10));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  monitor->display_statistics(false);

  LOG_INFO("Phase 2: High load");
  for (int i = 0; i < 50000; ++i) {
    if (i % 10 == 0)
      producer1->send_critical_message();
    if (i % 10 < 6)
      producer2->send_normal_message();
    if (i % 10 < 3)
      producer3->send_debug_message();
    if (i % 20 == 0)
      std::this_thread::sleep_for(std::chrono::microseconds(2));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  monitor->display_statistics(false);

  LOG_INFO("Draining queue...");
  std::this_thread::sleep_for(std::chrono::seconds(2));
  monitor->display_statistics(false);

  running.store(false, std::memory_order_relaxed);
  worker.join();
  LOG_INFO("Demo completed!");
}

int main(int argc, char* argv[]) {
  bool stress_mode = false;

  if (argc > 1) {
    if (std::strcmp(argv[1], "--stress") == 0 || std::strcmp(argv[1], "-s") == 0) {
      stress_mode = true;
    } else if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
      LOG_INFO("Usage: %s [OPTIONS]", argv[0]);
      LOG_INFO("  --stress, -s    Run stress test mode");
      LOG_INFO("  --help, -h      Show this help");
      return 0;
    }
  }

  if (stress_mode) {
    run_stress_test();
  } else {
    run_demo_mode();
  }

  return 0;
}
