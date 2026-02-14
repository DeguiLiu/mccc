#include <catch2/catch_test_macros.hpp>
#include <mccc/static_component.hpp>

#include <variant>

namespace {

struct SensorData {
  float temperature{0.0f};
};
struct MotorCmd {
  int speed{0};
};
struct LogMsg {
  int level{0};
};

using TestPayload = std::variant<SensorData, MotorCmd, LogMsg>;
using TestBus = mccc::AsyncBus<TestPayload>;

class TestComponent : public mccc::StaticComponent<TestComponent, TestPayload> {
 public:
  int sensor_count = 0;
  int motor_count = 0;
  float last_temp = 0.0f;
  int last_speed = 0;

  void Handle(const SensorData& d) noexcept {
    ++sensor_count;
    last_temp = d.temperature;
  }

  void Handle(const MotorCmd& c) noexcept {
    ++motor_count;
    last_speed = c.speed;
  }

  // LogMsg intentionally NOT handled -> silently ignored
};

}  // namespace

TEST_CASE("StaticComponent dispatches to Handle methods", "[StaticComponent]") {
  auto& bus = TestBus::Instance();
  TestComponent comp;
  auto visitor = comp.MakeVisitor();

  // Drain any existing messages
  while (bus.ProcessBatchWith(visitor) > 0) {}

  bus.Publish(SensorData{25.5f}, 1);
  bus.Publish(MotorCmd{100}, 1);
  bus.Publish(LogMsg{3}, 1);  // should be ignored

  uint32_t processed = bus.ProcessBatchWith(visitor);
  REQUIRE(processed == 3);
  REQUIRE(comp.sensor_count == 1);
  REQUIRE(comp.motor_count == 1);
  REQUIRE(comp.last_temp == 25.5f);
  REQUIRE(comp.last_speed == 100);
}

TEST_CASE("StaticComponent ignores unhandled types", "[StaticComponent]") {
  auto& bus = TestBus::Instance();
  TestComponent comp;
  auto visitor = comp.MakeVisitor();

  // Drain
  while (bus.ProcessBatchWith(visitor) > 0) {}

  // Only publish LogMsg (not handled by TestComponent)
  bus.Publish(LogMsg{1}, 1);
  bus.Publish(LogMsg{2}, 1);

  uint32_t processed = bus.ProcessBatchWith(visitor);
  REQUIRE(processed == 2);
  REQUIRE(comp.sensor_count == 0);
  REQUIRE(comp.motor_count == 0);
}

TEST_CASE("HasHandler trait detection", "[StaticComponent]") {
  STATIC_REQUIRE(mccc::detail::HasHandler<TestComponent, SensorData>::value);
  STATIC_REQUIRE(mccc::detail::HasHandler<TestComponent, MotorCmd>::value);
  STATIC_REQUIRE_FALSE(mccc::detail::HasHandler<TestComponent, LogMsg>::value);
}

TEST_CASE("StaticComponent multiple process rounds", "[StaticComponent]") {
  auto& bus = TestBus::Instance();
  TestComponent comp;
  auto visitor = comp.MakeVisitor();

  // Drain
  while (bus.ProcessBatchWith(visitor) > 0) {}

  // Round 1
  bus.Publish(SensorData{1.0f}, 1);
  bus.ProcessBatchWith(visitor);
  REQUIRE(comp.sensor_count == 1);

  // Round 2
  bus.Publish(SensorData{2.0f}, 1);
  bus.Publish(MotorCmd{50}, 1);
  bus.ProcessBatchWith(visitor);
  REQUIRE(comp.sensor_count == 2);
  REQUIRE(comp.motor_count == 1);
  REQUIRE(comp.last_temp == 2.0f);
}
