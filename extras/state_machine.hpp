/**
 * @file state_machine.hpp
 * @brief Clean C++17 template-based hierarchical state machine.
 *
 * Design principles:
 * - Template for type-safe user data (no void* casting)
 * - std::reference_wrapper for non-owning references
 * - std::optional for optional parent
 * - std::function for flexible callbacks
 * - Minimal inheritance, maximum composition
 *
 * MISRA C++ Compliance:
 * - Rule 5-0-13: Explicit boolean comparisons
 * - Rule 6-3-1: All if statements have braces
 * - Rule 12-8-1: Copy operations properly defined
 *
 * Naming convention (Google C++ Style Guide):
 * - Accessors: lowercase (e.g., id(), name(), parent())
 * - Mutators: set_xxx() (e.g., set_parent(), set_on_entry())
 * - Regular functions: PascalCase (e.g., Dispatch(), Reset())
 */

#ifndef INCLUDE_STATE_MACHINE_HPP_
#define INCLUDE_STATE_MACHINE_HPP_

#include <cassert>
#include <cstdint>

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace hsm {

// ============================================================================
// Overloaded Pattern for std::visit (C++14 compatible)
// ============================================================================

template <class... Ts>
struct overloaded;

template <class T>
struct overloaded<T> : T {
  using T::operator();
  explicit overloaded(T t) : T(std::move(t)) {}
};

template <class T, class... Ts>
struct overloaded<T, Ts...> : T, overloaded<Ts...> {
  using T::operator();
  using overloaded<Ts...>::operator();
  explicit overloaded(T t, Ts... ts) : T(std::move(t)), overloaded<Ts...>(std::move(ts)...) {}
};

/** @brief Helper to construct overloaded visitor (replaces C++17 deduction guide) */
template <class... Ts>
overloaded<Ts...> make_overloaded(Ts... ts) {
  return overloaded<Ts...>(std::move(ts)...);
}

// ============================================================================
// Event System
// ============================================================================

/**
 * @brief Event class - simple value type.
 */
class Event final {
 public:
  explicit constexpr Event(uint32_t id) noexcept : id_(id) {}

  // Accessor: lowercase
  constexpr uint32_t id() const noexcept { return id_; }

 private:
  uint32_t id_;
};

/**
 * @brief Example: Typed Event using std::variant for type-safe event data.
 *
 * This demonstrates how to extend the state machine with typed events.
 * Users can define their own event data types and use std::visit for pattern
 * matching.
 *
 * Example usage:
 *   using MyEventData = std::variant<ButtonClick, MouseMove, KeyPress>;
 *   TypedEvent<MyEventData> event(1, ButtonClick{x, y});
 *
 *   event.Visit(make_overloaded(
 *       [](const ButtonClick& e) { ... },
 *       [](const MouseMove& e) { ... },
 *       [](const KeyPress& e) { ... }
 *   });
 */
template <typename EventDataVariant>
class TypedEvent final {
 public:
  explicit TypedEvent(uint32_t id,
                      EventDataVariant data) noexcept(std::is_nothrow_move_constructible_v<EventDataVariant>)
      : id_(id), data_(std::move(data)) {}

  // Accessor: lowercase
  constexpr uint32_t id() const noexcept { return id_; }

  // Get data using std::get (throws if wrong type)
  template <typename T>
  const T& get_data() const {
    return std::get<T>(data_);
  }

  // Try to get data (returns nullptr if wrong type) - MISRA compliant
  template <typename T>
  const T* try_get_data() const noexcept {
    return std::get_if<T>(&data_);
  }

  // Visit data with a visitor (std::visit pattern)
  template <typename Visitor>
  auto Visit(Visitor&& visitor) const {
    return std::visit(std::forward<Visitor>(visitor), data_);
  }

  // Accessor: lowercase
  const EventDataVariant& variant() const noexcept { return data_; }

 private:
  uint32_t id_;
  EventDataVariant data_;
};

/**
 * @brief Transition type enumeration.
 */
enum class TransitionType : uint8_t {
  kExternal = 0, /**< Exit source, execute action, enter target */
  kInternal      /**< Execute action only, no state change */
};

// Forward declaration
template <typename Context>
class StateMachine;

/**
 * @brief State class template.
 * @tparam Context User-defined context type for type-safe data access.
 */
template <typename Context>
class State final {
 public:
  using ActionFn = std::function<void(Context&, const Event&)>;
  using GuardFn = std::function<bool(const Context&, const Event&)>;
  using DefaultHandlerFn = std::function<bool(Context&, const Event&)>;

  /**
   * @brief Transition definition.
   */
  struct Transition {
    uint32_t event_id;
    State* target;  // nullptr for internal transitions
    GuardFn guard;
    ActionFn action;
    TransitionType type;

    Transition(uint32_t id, State* tgt, GuardFn g, ActionFn a, TransitionType t)
        : event_id(id), target(tgt), guard(std::move(g)), action(std::move(a)), type(t) {}
  };

  explicit State(std::string name = "") : name_(std::move(name)) {}

  // Non-copyable, movable
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) noexcept = default;
  State& operator=(State&&) noexcept = default;

  // --- Configuration API (Mutators: set_xxx) ---

  State& set_parent(State& parent) noexcept {
    parent_ = &parent;
    return *this;
  }

  State& set_on_entry(ActionFn action) {
    entry_action_ = std::move(action);
    return *this;
  }

  State& set_on_exit(ActionFn action) {
    exit_action_ = std::move(action);
    return *this;
  }

  /**
   * @brief Set a default event handler for this state (HSM parent handler)
   *
   * This handler is called when an event is not handled by specific
   * transitions. In HSM, parent states typically define this to handle events
   * that child states don't handle.
   *
   * @param handler Function to handle unmatched events, returns true if handled
   */
  State& set_default_handler(DefaultHandlerFn handler) {
    default_event_handler_ = std::move(handler);
    return *this;
  }

  // --- Transition API (Regular functions: PascalCase) ---

  State& AddTransition(uint32_t event_id, State& target, ActionFn action = nullptr) {
    transitions_.emplace_back(event_id, &target, nullptr, std::move(action), TransitionType::kExternal);
    return *this;
  }

  State& AddTransition(uint32_t event_id, State& target, GuardFn guard, ActionFn action) {
    transitions_.emplace_back(event_id, &target, std::move(guard), std::move(action), TransitionType::kExternal);
    return *this;
  }

  State& AddInternalTransition(uint32_t event_id, ActionFn action) {
    transitions_.emplace_back(event_id, nullptr, nullptr, std::move(action), TransitionType::kInternal);
    return *this;
  }

  State& AddInternalTransition(uint32_t event_id, GuardFn guard, ActionFn action) {
    transitions_.emplace_back(event_id, nullptr, std::move(guard), std::move(action), TransitionType::kInternal);
    return *this;
  }

  // --- Query API (Accessors: lowercase) ---

  const std::string& name() const noexcept { return name_; }
  State* parent() const noexcept { return parent_; }
  bool has_parent() const noexcept { return parent_ != nullptr; }
  bool has_default_handler() const noexcept { return default_event_handler_ != nullptr; }

  uint8_t depth() const noexcept {
    uint8_t d = 0U;
    for (const State* s = this; s != nullptr; s = s->parent_) {
      ++d;
    }
    return d;
  }

  // --- Internal API (used by StateMachine) ---

  void ExecuteEntry(Context& ctx, const Event& event) const noexcept {
    // MISRA 5-0-13: Explicit boolean comparison for std::function
    if (entry_action_ != nullptr) {
      entry_action_(ctx, event);
    }
  }

  void ExecuteExit(Context& ctx, const Event& event) const noexcept {
    // MISRA 5-0-13: Explicit boolean comparison for std::function
    if (exit_action_ != nullptr) {
      exit_action_(ctx, event);
    }
  }

  const Transition* FindTransition(const Context& ctx, const Event& event) const noexcept {
    for (const auto& t : transitions_) {
      if (t.event_id == event.id()) {
        // MISRA 5-0-13: Explicit boolean comparison for std::function
        if (t.guard != nullptr) {
          if (t.guard(ctx, event) == true) {
            return &t;
          }
        } else {
          return &t;
        }
      }
    }
    return nullptr;
  }

  /**
   * @brief Try to handle event with default handler (HSM parent handler)
   * @return true if event was handled by default handler
   */
  bool TryDefaultHandler(Context& ctx, const Event& event) const noexcept {
    if (default_event_handler_ != nullptr) {
      try {
        return default_event_handler_(ctx, event);
      } catch (...) {
        // MISRA: Catch all exceptions
        return false;
      }
    }
    return false;
  }

 private:
  std::string name_;
  State* parent_ = nullptr;
  ActionFn entry_action_;
  ActionFn exit_action_;
  std::vector<Transition> transitions_;
  DefaultHandlerFn default_event_handler_;
};

/**
 * @brief Hierarchical State Machine template.
 * @tparam Context User-defined context type.
 */
template <typename Context>
class StateMachine final {
 public:
  using StateType = State<Context>;
  using UnhandledEventFn = std::function<void(Context&, const Event&)>;

  explicit StateMachine(StateType& initial_state, Context& context, uint32_t max_depth = 16U)
      : current_state_(&initial_state), initial_state_(&initial_state), context_(context), max_depth_(max_depth) {
    entry_path_.reserve(max_depth);
    EnterInitialState();
  }

  // Non-copyable, movable
  StateMachine(const StateMachine&) = delete;
  StateMachine& operator=(const StateMachine&) = delete;
  StateMachine(StateMachine&&) noexcept = default;
  StateMachine& operator=(StateMachine&&) noexcept = default;

  // --- Public API (Regular functions: PascalCase) ---

  bool Dispatch(const Event& event) noexcept {
    // Bubble up through hierarchy - first try specific transitions
    for (StateType* s = current_state_; s != nullptr; s = s->parent()) {
      const typename StateType::Transition* t = s->FindTransition(context_, event);
      if (t != nullptr) {
        ExecuteTransition(*t, event);
        return true;
      }
    }

    // Bubble up again - try default event handlers (HSM parent handler pattern)
    for (StateType* s = current_state_; s != nullptr; s = s->parent()) {
      if (s->TryDefaultHandler(context_, event)) {
        return true;
      }
    }

    // Unhandled
    // MISRA 5-0-13: Explicit boolean comparison for std::function
    if (unhandled_event_fn_ != nullptr) {
      unhandled_event_fn_(context_, event);
    }
    return false;
  }

  void Reset() noexcept { TransitionTo(*initial_state_, nullptr); }

  bool IsInState(const StateType& state) const noexcept {
    for (const StateType* s = current_state_; s != nullptr; s = s->parent()) {
      // MISRA 6-3-1: All if statements must have braces
      if (s == &state) {
        return true;
      }
    }
    return false;
  }

  // --- Accessors (lowercase) ---

  StateType& current_state() const noexcept { return *current_state_; }
  const std::string& current_state_name() const noexcept { return current_state_->name(); }
  Context& context() noexcept { return context_; }
  const Context& context() const noexcept { return context_; }

  // --- Mutator (set_xxx) ---

  void set_unhandled_event_handler(UnhandledEventFn fn) noexcept { unhandled_event_fn_ = std::move(fn); }

 private:
  void EnterInitialState() noexcept {
    // Build path from initial state to root
    BuildEntryPath(*initial_state_, nullptr);

    // Execute entry actions from root to initial state
    for (auto it = entry_path_.rbegin(); it != entry_path_.rend(); ++it) {
      // Use a dummy event for initial entry
      static const Event kDummyEvent(0U);
      // MISRA: Check pointer before dereferencing
      if (*it != nullptr) {
        (*it)->ExecuteEntry(context_, kDummyEvent);
      }
    }
  }

  void ExecuteTransition(const typename StateType::Transition& t, const Event& event) noexcept {
    // Execute action
    // MISRA 5-0-13: Explicit boolean comparison for std::function
    if (t.action != nullptr) {
      t.action(context_, event);
    }

    // For external transitions, change state
    if ((t.type == TransitionType::kExternal) && (t.target != nullptr)) {
      TransitionTo(*t.target, &event);
    }
  }

  void TransitionTo(StateType& target, const Event* event) noexcept {
    StateType* source = current_state_;

    // Self-transition
    if (source == &target) {
      if (event != nullptr) {
        source->ExecuteExit(context_, *event);
        source->ExecuteEntry(context_, *event);
      }
      return;
    }

    // Find LCA
    StateType* lca = FindLCA(*source, target);

    // Exit from source to LCA
    if (event != nullptr) {
      for (StateType* s = source; s != nullptr && s != lca; s = s->parent()) {
        s->ExecuteExit(context_, *event);
      }
    }

    // Build entry path
    BuildEntryPath(target, lca);

    // Update current state
    current_state_ = &target;

    // Enter from LCA to target
    if (event != nullptr) {
      for (auto it = entry_path_.rbegin(); it != entry_path_.rend(); ++it) {
        // MISRA: Check pointer before dereferencing
        if (*it != nullptr) {
          (*it)->ExecuteEntry(context_, *event);
        }
      }
    }
  }

  StateType* FindLCA(StateType& s1, StateType& s2) const noexcept {
    // Normalize depths
    StateType* p1 = &s1;
    StateType* p2 = &s2;
    uint8_t d1 = p1->depth();
    uint8_t d2 = p2->depth();

    while (d1 > d2) {
      p1 = p1->parent();
      --d1;
    }
    while (d2 > d1) {
      p2 = p2->parent();
      --d2;
    }

    // Find common ancestor
    while (p1 != p2) {
      p1 = p1->parent();
      p2 = p2->parent();
    }
    return p1;
  }

  void BuildEntryPath(StateType& target, StateType* lca) noexcept {
    entry_path_.clear();
    for (StateType* s = &target; s != nullptr && s != lca; s = s->parent()) {
      entry_path_.push_back(s);
    }
    // MISRA: Use assert for debug-time checks (allowed in MISRA C++)
    assert(entry_path_.size() <= max_depth_);
  }

  StateType* current_state_;
  StateType* initial_state_;
  Context& context_;
  uint32_t max_depth_;
  std::vector<StateType*> entry_path_;
  UnhandledEventFn unhandled_event_fn_;
};

} /* namespace hsm */

#endif /* INCLUDE_STATE_MACHINE_HPP_ */
