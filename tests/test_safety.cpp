/**
 * @file test_safety.cpp
 * @brief Functional-safety behavior tests for AsyncBus.
 */

#include <cstdint>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <mccc/component.hpp>
#include <mccc/mccc.hpp>
#include <memory>
#include <stdexcept>

namespace {

std::atomic<int> g_queue_full{0};
std::atomic<int> g_callbacks_full{0};
std::atomic<int> g_reentrant_consumer{0};
std::atomic<int> g_not_initialized{0};
std::atomic<int> g_reentrant_subscribe{0};
std::atomic<int> g_context_hits{0};
std::atomic<int> g_processing_errors{0};
void* g_expected_context = nullptr;

void SafetyErrorHandler(mccc::BusError error, uint64_t /*msg_id*/) noexcept {
  switch (error) {
    case mccc::BusError::QUEUE_FULL:
      g_queue_full.fetch_add(1, std::memory_order_relaxed);
      break;
    case mccc::BusError::CALLBACKS_FULL:
      g_callbacks_full.fetch_add(1, std::memory_order_relaxed);
      break;
    case mccc::BusError::REENTRANT_CONSUMER:
      g_reentrant_consumer.fetch_add(1, std::memory_order_relaxed);
      break;
    case mccc::BusError::NOT_INITIALIZED:
      g_not_initialized.fetch_add(1, std::memory_order_relaxed);
      break;
    case mccc::BusError::REENTRANT_SUBSCRIBE:
      g_reentrant_subscribe.fetch_add(1, std::memory_order_relaxed);
      break;
    case mccc::BusError::PROCESSING_ERROR:
      g_processing_errors.fetch_add(1, std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

void ContextErrorHandler(void* context, mccc::BusError error, uint64_t /*msg_id*/) noexcept {
  if (context == g_expected_context && error == mccc::BusError::QUEUE_FULL) {
    g_context_hits.fetch_add(1, std::memory_order_relaxed);
  }
}

void ResetSafetyCounters() {
  g_queue_full.store(0, std::memory_order_relaxed);
  g_callbacks_full.store(0, std::memory_order_relaxed);
  g_reentrant_consumer.store(0, std::memory_order_relaxed);
  g_not_initialized.store(0, std::memory_order_relaxed);
  g_reentrant_subscribe.store(0, std::memory_order_relaxed);
  g_context_hits.store(0, std::memory_order_relaxed);
  g_processing_errors.store(0, std::memory_order_relaxed);
}

template <typename Bus>
void DrainBus(Bus& bus) {
  while (bus.ProcessBatch() > 0U) {}
}

struct LifecycleMsg {
  uint32_t value;
};
using LifecyclePayload = std::variant<LifecycleMsg>;
using LifecycleBus = mccc::AsyncBus<LifecyclePayload>;

struct FullMsg {
  uint32_t value;
};
using FullPayload = std::variant<FullMsg>;
using FullBus = mccc::AsyncBus<FullPayload>;

struct SubscribeFullMsg {
  uint32_t value;
};
using SubscribeFullPayload = std::variant<SubscribeFullMsg>;
using SubscribeFullBus = mccc::AsyncBus<SubscribeFullPayload>;

struct ReentrantMsg {
  uint32_t value;
};
using ReentrantPayload = std::variant<ReentrantMsg>;
using ReentrantBus = mccc::AsyncBus<ReentrantPayload>;

struct HealthMsg {
  uint32_t value;
};
using HealthPayload = std::variant<HealthMsg>;
using HealthBus = mccc::AsyncBus<HealthPayload>;

struct IsolatedMsg {
  uint32_t value;
};
using IsolatedPayload = std::variant<IsolatedMsg>;
using IsolatedBus = mccc::AsyncBus<IsolatedPayload>;

struct DeferredMsg {
  uint32_t value;
};
using DeferredPayload = std::variant<DeferredMsg>;
using DeferredBus = mccc::AsyncBus<DeferredPayload>;

struct CallbackSubMsg {
  uint32_t value;
};
using CallbackSubPayload = std::variant<CallbackSubMsg>;
using CallbackSubBus = mccc::AsyncBus<CallbackSubPayload>;

struct ContextMsg {
  uint32_t value;
};
using ContextPayload = std::variant<ContextMsg>;
using ContextBus = mccc::AsyncBus<ContextPayload>;

struct InjectedMsg {
  uint32_t value;
};
using InjectedPayload = std::variant<InjectedMsg>;
using InjectedBus = mccc::AsyncBus<InjectedPayload>;

struct InjectedComponent : mccc::Component<InjectedPayload> {
  explicit InjectedComponent(InjectedBus& bus) : mccc::Component<InjectedPayload>(bus) {}

  void Start() {
    SubscribeSimple<InjectedMsg>(
        [this](const InjectedMsg&, const mccc::MessageHeader&) { count.fetch_add(1U, std::memory_order_relaxed); });
  }

  std::atomic<uint32_t> count{0U};
};

struct ThrowingMsg {
  uint32_t value;
};
using ThrowingPayload = std::variant<ThrowingMsg>;
using ThrowingBus = mccc::AsyncBus<ThrowingPayload>;

}  // namespace

TEST_CASE("Shutdown rejects Publish and Init restores it", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = LifecycleBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  bus.Shutdown();
  REQUIRE_FALSE(bus.IsRunning());
  REQUIRE_FALSE(bus.Publish(LifecyclePayload{LifecycleMsg{1U}}, 1U));
  REQUIRE(g_not_initialized.load(std::memory_order_relaxed) == 1);

  bus.Init();
  REQUIRE(bus.IsRunning());
  REQUIRE(bus.Publish(LifecyclePayload{LifecycleMsg{2U}}, 1U));
  DrainBus(bus);
}

TEST_CASE("Subscribe reports CALLBACKS_FULL instead of failing silently", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = SubscribeFullBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  mccc::SubscriptionHandle handles[MCCC_MAX_CALLBACKS_PER_TYPE];
  for (uint32_t i = 0U; i < MCCC_MAX_CALLBACKS_PER_TYPE; ++i) {
    handles[i] = bus.Subscribe<SubscribeFullMsg>([](const auto&) {});
    REQUIRE(handles[i].IsValid());
  }

  auto overflow = bus.Subscribe<SubscribeFullMsg>([](const auto&) {});
  REQUIRE_FALSE(overflow.IsValid());
  REQUIRE(g_callbacks_full.load(std::memory_order_relaxed) == 1);

  for (auto& handle : handles) {
    REQUIRE(bus.Unsubscribe(handle));
  }
}

TEST_CASE("Queue overflow is reported regardless of performance mode", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = FullBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  // Ignored in safety builds, which stay in FULL_FEATURED.
  bus.SetPerformanceMode(FullBus::PerformanceMode::BARE_METAL);
  DrainBus(bus);
  g_queue_full.store(0, std::memory_order_relaxed);

  uint32_t published = 0U;
  for (uint32_t i = 0U; i < FullBus::MAX_QUEUE_DEPTH; ++i) {
    if (!bus.Publish(FullPayload{FullMsg{i}}, 1U)) {
      break;
    }
    ++published;
  }
  REQUIRE(published > 0U);
  REQUIRE(published <= FullBus::MAX_QUEUE_DEPTH);

  REQUIRE_FALSE(bus.Publish(FullPayload{FullMsg{0U}}, 1U));
  REQUIRE(g_queue_full.load(std::memory_order_relaxed) > 0);

  bus.SetPerformanceMode(FullBus::PerformanceMode::FULL_FEATURED);
  DrainBus(bus);
}

TEST_CASE("Reentrant ProcessBatch is rejected", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = ReentrantBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  std::atomic<uint32_t> inner_result{999U};
  auto handle = bus.Subscribe<ReentrantMsg>(
      [&bus, &inner_result](const auto&) { inner_result.store(bus.ProcessBatch(), std::memory_order_relaxed); });
  REQUIRE(handle.IsValid());

  REQUIRE(bus.Publish(ReentrantPayload{ReentrantMsg{1U}}, 1U));
  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE(inner_result.load(std::memory_order_relaxed) == 0U);
  REQUIRE(g_reentrant_consumer.load(std::memory_order_relaxed) == 1);

  REQUIRE(bus.Unsubscribe(handle));
}

TEST_CASE("Consumer progress cursor detects a stalled consumer", "[Safety]") {
  auto& bus = HealthBus::Instance();
  DrainBus(bus);
  uint32_t sampled = bus.ConsumerPosition();
  REQUIRE(bus.HasConsumerProgress(sampled));

  REQUIRE(bus.Publish(HealthPayload{HealthMsg{1U}}, 1U));
  REQUIRE_FALSE(bus.HasConsumerProgress(sampled));

  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE(bus.ConsumerPosition() != sampled);
  REQUIRE(bus.HasConsumerProgress(sampled));
}

TEST_CASE("Unsubscribe from a callback is deferred until the message returns", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = DeferredBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  std::atomic<uint32_t> calls{0U};
  std::atomic<bool> unsubscribed{false};
  mccc::SubscriptionHandle handle;
  handle = bus.Subscribe<DeferredMsg>([&bus, &handle, &calls, &unsubscribed](const auto&) {
    calls.fetch_add(1U, std::memory_order_relaxed);
    unsubscribed.store(bus.Unsubscribe(handle), std::memory_order_relaxed);
  });
  REQUIRE(handle.IsValid());

  REQUIRE(bus.Publish(DeferredPayload{DeferredMsg{1U}}, 1U));
  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE(calls.load(std::memory_order_relaxed) == 1U);
  REQUIRE(unsubscribed.load(std::memory_order_relaxed));

  REQUIRE(bus.Publish(DeferredPayload{DeferredMsg{2U}}, 1U));
  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE(calls.load(std::memory_order_relaxed) == 1U);
}

TEST_CASE("Subscribe from a callback is rejected without deadlock", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = CallbackSubBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  std::atomic<bool> nested_valid{true};
  auto handle = bus.Subscribe<CallbackSubMsg>([&bus, &nested_valid](const auto&) {
    auto nested = bus.Subscribe<CallbackSubMsg>([](const auto&) {});
    nested_valid.store(nested.IsValid(), std::memory_order_relaxed);
  });
  REQUIRE(handle.IsValid());

  REQUIRE(bus.Publish(CallbackSubPayload{CallbackSubMsg{1U}}, 1U));
  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE_FALSE(nested_valid.load(std::memory_order_relaxed));
  REQUIRE(g_reentrant_subscribe.load(std::memory_order_relaxed) == 1);

  REQUIRE(bus.Unsubscribe(handle));
}

TEST_CASE("Standalone bus instances are isolated from the singleton", "[Safety]") {
  auto isolated = std::make_unique<IsolatedBus>();
  std::atomic<uint32_t> received{0U};
  auto handle = isolated->Subscribe<IsolatedMsg>([&received](const auto&) { received.fetch_add(1U); });
  REQUIRE(handle.IsValid());

  REQUIRE(isolated->Publish(IsolatedPayload{IsolatedMsg{7U}}, 1U));
  REQUIRE(isolated->ProcessBatch() == 1U);
  REQUIRE(received.load() == 1U);
  REQUIRE(IsolatedBus::Instance().QueueDepth() == 0U);

  REQUIRE(isolated->Unsubscribe(handle));
}

TEST_CASE("Component can bind to a standalone bus", "[Safety]") {
  auto bus = std::make_unique<InjectedBus>();
  auto component = std::make_shared<InjectedComponent>(*bus);
  component->Start();

  REQUIRE(bus->Publish(InjectedPayload{InjectedMsg{5U}}, 1U));
  REQUIRE(bus->ProcessBatch() == 1U);
  REQUIRE(component->count.load(std::memory_order_relaxed) == 1U);
  REQUIRE(InjectedBus::Instance().QueueDepth() == 0U);

  component.reset();
  REQUIRE(bus->Publish(InjectedPayload{InjectedMsg{6U}}, 1U));
  REQUIRE(bus->ProcessBatch() == 1U);
}

TEST_CASE("Extended error callback receives its context", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = ContextBus::Instance();
  char marker = 0;
  g_expected_context = &marker;
  bus.SetErrorCallback(&ContextErrorHandler, &marker);
  DrainBus(bus);

  for (uint32_t i = 0U; i < ContextBus::MAX_QUEUE_DEPTH; ++i) {
    if (!bus.Publish(ContextPayload{ContextMsg{i}}, 1U)) {
      break;
    }
  }
  REQUIRE(g_context_hits.load(std::memory_order_relaxed) > 0);

  bus.SetErrorCallback(nullptr);
  g_expected_context = nullptr;
  DrainBus(bus);
}

TEST_CASE("A throwing callback becomes PROCESSING_ERROR, not terminate", "[Safety]") {
  ResetSafetyCounters();
  auto& bus = ThrowingBus::Instance();
  bus.SetErrorCallback(&SafetyErrorHandler);
  DrainBus(bus);

  auto handle = bus.Subscribe<ThrowingMsg>([](const auto&) { throw std::runtime_error("boom"); });
  REQUIRE(handle.IsValid());

  REQUIRE(bus.Publish(ThrowingPayload{ThrowingMsg{1U}}, 1U));
  REQUIRE(bus.ProcessBatch() == 1U);
  REQUIRE(g_processing_errors.load(std::memory_order_relaxed) == 1);

  REQUIRE(bus.Unsubscribe(handle));
}
