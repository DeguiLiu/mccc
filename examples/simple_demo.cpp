/**
 * @file simple_demo.cpp
 * @brief Minimal MCCC usage example.
 *
 * Demonstrates:
 * - Defining custom message types
 * - Publishing and subscribing
 * - Processing messages with a worker thread
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#include "example_types.hpp"
#include "log_macro.hpp"

#include <atomic>
#include <chrono>
#include <mccc/component.hpp>
#include <thread>

using namespace example;
using namespace mccc;

int main() {
  LOG_INFO("========================================");
  LOG_INFO("   MCCC Simple Demo");
  LOG_INFO("========================================");

  std::atomic<bool> stop_worker{false};

  // Start worker thread (single consumer)
  std::thread worker([&stop_worker]() noexcept {
    while (!stop_worker.load(std::memory_order_acquire)) {
      uint32_t processed = ExampleBus::Instance().ProcessBatch();
      if (processed == 0U) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
    while (ExampleBus::Instance().ProcessBatch() > 0U) {}
  });

  // Subscribe to MotionData
  ExampleBus::Instance().Subscribe<MotionData>([](const ExampleEnvelope& env) {
    const auto* data = std::get_if<MotionData>(&env.payload);
    if (data != nullptr) {
      LOG_INFO("Received MotionData: x=%.1f y=%.1f z=%.1f vel=%.1f", data->x, data->y, data->z, data->velocity);
    }
  });

  // Subscribe to SystemLog
  ExampleBus::Instance().Subscribe<SystemLog>([](const ExampleEnvelope& env) {
    const auto* log = std::get_if<SystemLog>(&env.payload);
    if (log != nullptr) {
      LOG_INFO("Received SystemLog: level=%d content=%s", log->level, log->content.c_str());
    }
  });

  // Publish some messages
  LOG_INFO("");
  LOG_INFO("Publishing messages...");

  for (int i = 0; i < 5; ++i) {
    float fi = static_cast<float>(i);
    MotionData motion(fi, fi * 2.0f, fi * 3.0f, fi * 0.5f);
    ExampleBus::Instance().Publish(std::move(motion), /*sender_id=*/1U);
  }

  SystemLog log_msg(1, "Hello from MCCC!");
  ExampleBus::Instance().Publish(std::move(log_msg), /*sender_id=*/2U);

  // Wait for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Show statistics
  auto stats = ExampleBus::Instance().GetStatistics();
  LOG_INFO("");
  LOG_INFO("Statistics:");
  LOG_INFO("  Published: %lu", stats.messages_published);
  LOG_INFO("  Processed: %lu", stats.messages_processed);
  LOG_INFO("  Dropped:   %lu", stats.messages_dropped);

  // Cleanup
  stop_worker.store(true, std::memory_order_release);
  worker.join();

  LOG_INFO("");
  LOG_INFO("Demo completed!");
  return 0;
}
