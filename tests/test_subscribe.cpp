/**
 * @file test_subscribe.cpp
 * @brief Unit tests for Subscribe/Unsubscribe lifecycle.
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/component.hpp>

#include <atomic>
#include <memory>

struct SubMsgA { int value; };
struct SubMsgB { float data; };

using SubPayload = std::variant<SubMsgA, SubMsgB>;
using SubBus = mccc::AsyncBus<SubPayload>;
using SubEnvelope = mccc::MessageEnvelope<SubPayload>;
using SubComponent = mccc::Component<SubPayload>;

TEST_CASE("Subscribe returns valid handle", "[Subscribe]") {
  auto& bus = SubBus::Instance();

  auto handle = bus.Subscribe<SubMsgA>([](const SubEnvelope&) {});

  REQUIRE(handle.type_index == mccc::VariantIndex<SubMsgA, SubPayload>::value);
  REQUIRE(handle.callback_id != static_cast<size_t>(-1));
}

TEST_CASE("Unsubscribe stops callback", "[Subscribe]") {
  auto& bus = SubBus::Instance();
  bus.ResetStatistics();

  // Drain first
  while (bus.ProcessBatch() > 0U) {}

  std::atomic<int> count{0};

  auto handle = bus.Subscribe<SubMsgA>([&count](const SubEnvelope&) {
    count.fetch_add(1, std::memory_order_relaxed);
  });

  // Publish and process
  SubMsgA msg1{1};
  bus.Publish(std::move(msg1), 1U);
  bus.ProcessBatch();
  REQUIRE(count.load() == 1);

  // Unsubscribe
  REQUIRE(bus.Unsubscribe(handle));

  // Publish again - callback should NOT fire
  SubMsgA msg2{2};
  bus.Publish(std::move(msg2), 1U);
  bus.ProcessBatch();
  REQUIRE(count.load() == 1);  // Still 1
}

TEST_CASE("Unsubscribe with invalid handle returns false", "[Subscribe]") {
  auto& bus = SubBus::Instance();

  mccc::SubscriptionHandle invalid{999U, 999U};
  REQUIRE_FALSE(bus.Unsubscribe(invalid));
}

TEST_CASE("Multiple subscribers for same type", "[Subscribe]") {
  auto& bus = SubBus::Instance();
  bus.ResetStatistics();

  // Drain first
  while (bus.ProcessBatch() > 0U) {}

  std::atomic<int> count1{0};
  std::atomic<int> count2{0};

  auto h1 = bus.Subscribe<SubMsgA>([&count1](const SubEnvelope&) {
    count1.fetch_add(1, std::memory_order_relaxed);
  });

  auto h2 = bus.Subscribe<SubMsgA>([&count2](const SubEnvelope&) {
    count2.fetch_add(1, std::memory_order_relaxed);
  });

  SubMsgA msg{42};
  bus.Publish(std::move(msg), 1U);
  bus.ProcessBatch();

  REQUIRE(count1.load() == 1);
  REQUIRE(count2.load() == 1);

  // Unsubscribe one
  bus.Unsubscribe(h1);

  SubMsgA msg2{43};
  bus.Publish(std::move(msg2), 1U);
  bus.ProcessBatch();

  REQUIRE(count1.load() == 1);  // No change
  REQUIRE(count2.load() == 2);  // Incremented

  bus.Unsubscribe(h2);
}

// ============================================================================
// Component lifecycle tests
// ============================================================================

class TestComponent : public SubComponent {
 public:
  static std::shared_ptr<TestComponent> create() {
    auto ptr = std::shared_ptr<TestComponent>(new TestComponent());
    ptr->init();
    return ptr;
  }

  int get_count() const { return count_.load(std::memory_order_relaxed); }

 private:
  TestComponent() = default;

  void init() {
    InitializeComponent();
    SubscribeSafe<SubMsgA>(
        [](std::shared_ptr<SubComponent> self_base, const SubMsgA& msg, const mccc::MessageHeader&) {
          auto self = std::static_pointer_cast<TestComponent>(self_base);
          if (self) {
            self->count_.fetch_add(1, std::memory_order_relaxed);
          }
        });
  }

  std::atomic<int> count_{0};
};

TEST_CASE("Component auto-unsubscribes on destruction", "[Subscribe]") {
  auto& bus = SubBus::Instance();
  bus.ResetStatistics();

  // Drain first
  while (bus.ProcessBatch() > 0U) {}

  std::atomic<int> external_count{0};
  auto ext_handle = bus.Subscribe<SubMsgA>([&external_count](const SubEnvelope&) {
    external_count.fetch_add(1, std::memory_order_relaxed);
  });

  {
    auto component = TestComponent::create();

    SubMsgA msg{1};
    bus.Publish(std::move(msg), 1U);
    bus.ProcessBatch();

    REQUIRE(component->get_count() == 1);
    REQUIRE(external_count.load() == 1);

    // Component goes out of scope here - should auto-unsubscribe
  }

  // Publish again - component callback should NOT fire
  SubMsgA msg2{2};
  bus.Publish(std::move(msg2), 1U);
  bus.ProcessBatch();

  // External subscriber still works
  REQUIRE(external_count.load() == 2);

  bus.Unsubscribe(ext_handle);
}

TEST_CASE("SubscribeSimple works", "[Subscribe]") {
  auto& bus = SubBus::Instance();
  bus.ResetStatistics();

  // Drain first
  while (bus.ProcessBatch() > 0U) {}

  class SimpleComponent : public SubComponent {
   public:
    static std::shared_ptr<SimpleComponent> create() {
      auto ptr = std::shared_ptr<SimpleComponent>(new SimpleComponent());
      ptr->InitializeComponent();
      ptr->SubscribeSimple<SubMsgB>(
          [ptr](const SubMsgB& msg, const mccc::MessageHeader&) {
            ptr->last_value_ = msg.data;
          });
      return ptr;
    }
    float last_value_ = 0.0f;
   private:
    SimpleComponent() = default;
  };

  auto comp = SimpleComponent::create();

  SubMsgB msg{3.14f};
  bus.Publish(std::move(msg), 1U);
  bus.ProcessBatch();

  REQUIRE(comp->last_value_ == 3.14f);
}
