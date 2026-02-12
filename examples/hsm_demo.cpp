/**
 * @file hsm_demo.cpp
 * @brief MCCC + Hierarchical State Machine integration demo.
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#include "log_macro.hpp"
#include "example_types.hpp"
#include "state_machine.hpp"

#include <mccc/component.hpp>

#include <cinttypes>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace example;
using namespace mccc;

enum class RobotState : uint8_t { IDLE = 0U, RUNNING = 1U, PAUSED = 2U, ERROR = 3U };

constexpr const char* robot_state_to_string(RobotState state) noexcept {
  switch (state) {
    case RobotState::IDLE: return "IDLE";
    case RobotState::RUNNING: return "RUNNING";
    case RobotState::PAUSED: return "PAUSED";
    case RobotState::ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

namespace RobotEvents {
constexpr uint32_t START = 1U;
constexpr uint32_t STOP = 2U;
constexpr uint32_t PAUSE = 3U;
constexpr uint32_t RESUME = 4U;
constexpr uint32_t FAULT = 5U;
constexpr uint32_t RESET = 6U;
}  // namespace RobotEvents

struct RobotContext {
  std::atomic<RobotState> current_state{RobotState::IDLE};
  std::atomic<uint64_t> motion_count{0U};
  std::atomic<uint64_t> error_count{0U};
  float last_x{0.0F};
  float last_y{0.0F};
  float last_z{0.0F};
  bool verbose{true};
};

class RobotController : public ExampleComponent {
 public:
  RobotController(const RobotController&) = delete;
  RobotController& operator=(const RobotController&) = delete;
  RobotController(RobotController&&) = delete;
  RobotController& operator=(RobotController&&) = delete;

  static std::shared_ptr<RobotController> create() noexcept {
    std::shared_ptr<RobotController> ptr(new RobotController());
    ptr->init();
    return ptr;
  }

  void init() noexcept {
    InitializeComponent();
    setup_state_machine();

    SubscribeSafe<MotionData>([](std::shared_ptr<ExampleComponent> self_base,
                                const MotionData& data, const MessageHeader& header) noexcept {
      auto self = std::static_pointer_cast<RobotController>(self_base);
      if (self) self->on_motion(data, header);
    });
    LOG_INFO("[RobotController] Initialized with HSM");
  }

  bool start() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::START)) : false; }
  bool stop() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::STOP)) : false; }
  bool pause() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::PAUSE)) : false; }
  bool resume() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::RESUME)) : false; }
  bool trigger_fault() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::FAULT)) : false; }
  bool reset() noexcept { return hsm_ ? hsm_->Dispatch(hsm::Event(RobotEvents::RESET)) : false; }

  RobotState get_state() const noexcept { return context_.current_state.load(std::memory_order_acquire); }
  const char* get_state_name() const noexcept { return robot_state_to_string(get_state()); }
  uint64_t get_motion_count() const noexcept { return context_.motion_count.load(std::memory_order_relaxed); }
  void set_verbose(bool v) noexcept { context_.verbose = v; }

  ~RobotController() override = default;

 private:
  RobotController() noexcept = default;

  void setup_state_machine() noexcept {
    idle_state_ = std::make_unique<hsm::State<RobotContext>>("IDLE");
    running_state_ = std::make_unique<hsm::State<RobotContext>>("RUNNING");
    paused_state_ = std::make_unique<hsm::State<RobotContext>>("PAUSED");
    error_state_ = std::make_unique<hsm::State<RobotContext>>("ERROR");

    idle_state_->set_on_entry([](RobotContext& ctx, const hsm::Event&) {
      ctx.current_state.store(RobotState::IDLE, std::memory_order_release);
      if (ctx.verbose) LOG_INFO("[HSM] -> IDLE");
    });
    idle_state_->AddTransition(RobotEvents::START, *running_state_);

    running_state_->set_on_entry([](RobotContext& ctx, const hsm::Event&) {
      ctx.current_state.store(RobotState::RUNNING, std::memory_order_release);
      if (ctx.verbose) LOG_INFO("[HSM] -> RUNNING");
    });
    running_state_->AddTransition(RobotEvents::STOP, *idle_state_);
    running_state_->AddTransition(RobotEvents::PAUSE, *paused_state_);
    running_state_->AddTransition(RobotEvents::FAULT, *error_state_,
        [](RobotContext& ctx, const hsm::Event&) { ctx.error_count.fetch_add(1U, std::memory_order_relaxed); });

    paused_state_->set_on_entry([](RobotContext& ctx, const hsm::Event&) {
      ctx.current_state.store(RobotState::PAUSED, std::memory_order_release);
      if (ctx.verbose) LOG_INFO("[HSM] -> PAUSED");
    });
    paused_state_->AddTransition(RobotEvents::RESUME, *running_state_);
    paused_state_->AddTransition(RobotEvents::STOP, *idle_state_);

    error_state_->set_on_entry([](RobotContext& ctx, const hsm::Event&) {
      ctx.current_state.store(RobotState::ERROR, std::memory_order_release);
      if (ctx.verbose) LOG_INFO("[HSM] -> ERROR");
    });
    error_state_->AddTransition(RobotEvents::RESET, *idle_state_);

    hsm_ = std::make_unique<hsm::StateMachine<RobotContext>>(*idle_state_, context_);
  }

  void on_motion(const MotionData& data, const MessageHeader&) noexcept {
    if (context_.current_state.load(std::memory_order_acquire) == RobotState::RUNNING) {
      context_.last_x = data.x;
      context_.last_y = data.y;
      context_.last_z = data.z;
      context_.motion_count.fetch_add(1U, std::memory_order_relaxed);
    }
  }

  RobotContext context_;
  std::unique_ptr<hsm::State<RobotContext>> idle_state_;
  std::unique_ptr<hsm::State<RobotContext>> running_state_;
  std::unique_ptr<hsm::State<RobotContext>> paused_state_;
  std::unique_ptr<hsm::State<RobotContext>> error_state_;
  std::unique_ptr<hsm::StateMachine<RobotContext>> hsm_;
};

void send_motion_commands(uint32_t count, uint32_t sender_id) noexcept {
  for (uint32_t i = 0U; i < count; ++i) {
    float fi = static_cast<float>(i);
    MotionData motion(fi * 0.1F, fi * 0.2F, fi * 0.3F, fi * 0.01F);
    ExampleBus::Instance().Publish(std::move(motion), sender_id);
  }
}

int main() {
  LOG_INFO("========================================");
  LOG_INFO("   MCCC + HSM Demo");
  LOG_INFO("========================================");

  std::atomic<bool> stop_worker{false};
  std::thread worker([&stop_worker]() noexcept {
    while (!stop_worker.load(std::memory_order_acquire)) {
      uint32_t processed = ExampleBus::Instance().ProcessBatch();
      if (processed == 0U) std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    while (ExampleBus::Instance().ProcessBatch() > 0U) {}
  });

  auto robot = RobotController::create();

  // Test state transitions
  LOG_INFO("\n--- Test: State Transitions ---");
  robot->start();
  send_motion_commands(100U, 100U);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  LOG_INFO("Motion count: %" PRIu64, robot->get_motion_count());

  robot->pause();
  uint64_t before = robot->get_motion_count();
  send_motion_commands(100U, 100U);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  LOG_INFO("Commands ignored in PAUSED: %s", (before == robot->get_motion_count()) ? "YES" : "NO");

  robot->resume();
  send_motion_commands(100U, 100U);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  LOG_INFO("Motion count after resume: %" PRIu64, robot->get_motion_count());

  // Test error handling
  LOG_INFO("\n--- Test: Error Handling ---");
  robot->trigger_fault();
  LOG_INFO("State: %s", robot->get_state_name());
  robot->reset();
  robot->start();
  LOG_INFO("State after reset+start: %s", robot->get_state_name());

  robot->stop();
  stop_worker.store(true, std::memory_order_release);
  worker.join();

  LOG_INFO("\nDemo completed!");
  return 0;
}
