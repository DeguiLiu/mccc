/**
 * MIT License
 *
 * Copyright (c) 2024 liudegui
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file static_component.hpp
 * @brief Zero-overhead CRTP component with compile-time dispatch.
 *
 * Alternative to Component when:
 * - Message handlers are known at compile time
 * - No dynamic subscribe/unsubscribe needed
 * - Used with AsyncBus::ProcessBatchWith() for zero-overhead dispatch
 */

#ifndef MCCC_STATIC_COMPONENT_HPP_
#define MCCC_STATIC_COMPONENT_HPP_

#include "mccc/mccc.hpp"

#include <type_traits>
#include <utility>

namespace mccc {

namespace detail {

/**
 * @brief Detect if Derived has a Handle(const T&) method.
 *
 * Uses SFINAE to check at compile time.
 */
template <typename Derived, typename T, typename = void>
struct HasHandler : std::false_type {};

template <typename Derived, typename T>
struct HasHandler<Derived, T, std::void_t<decltype(std::declval<Derived>().Handle(std::declval<const T&>()))>>
    : std::true_type {};

/**
 * @brief Generate visitor for a single type T.
 *
 * If Derived has Handle(const T&), dispatch to it. Otherwise ignore.
 */
template <typename Derived, typename PayloadVariant>
struct VisitorGenerator {
  Derived* self;

  explicit VisitorGenerator(Derived* s) noexcept : self(s) {}

  template <typename T>
  void operator()(const T& data) const noexcept {
    if constexpr (HasHandler<Derived, T>::value) {
      self->Handle(data);
    }
    // Types without Handle() are silently ignored (compile-time decision)
  }
};

}  // namespace detail

/**
 * @brief Zero-overhead CRTP component base.
 *
 * Usage:
 * @code
 * class MySensor : public StaticComponent<MySensor, MyPayload> {
 *  public:
 *   void Handle(const SensorData& d) noexcept { process(d); }
 *   void Handle(const MotorCmd& c) noexcept { execute(c); }
 *   // Types not handled are silently ignored
 * };
 *
 * MySensor sensor;
 * auto visitor = sensor.MakeVisitor();
 * bus.ProcessBatchWith(visitor);
 * @endcode
 *
 * Advantages over Component:
 * - No virtual destructor
 * - No std::shared_ptr / weak_ptr
 * - No std::function / FixedFunction
 * - No callback_table_ lookup
 * - No shared_mutex lock
 * - Handler calls are inlineable
 *
 * @tparam Derived The CRTP derived class
 * @tparam PayloadVariant A std::variant<...> of message types
 */
template <typename Derived, typename PayloadVariant>
class StaticComponent {
 public:
  using BusType = AsyncBus<PayloadVariant>;
  using EnvelopeType = MessageEnvelope<PayloadVariant>;

  /**
   * @brief Create a visitor that dispatches to Derived::Handle methods.
   *
   * The returned visitor can be passed to AsyncBus::ProcessBatchWith().
   */
  auto MakeVisitor() noexcept { return detail::VisitorGenerator<Derived, PayloadVariant>{static_cast<Derived*>(this)}; }

  /**
   * @brief Create a const visitor.
   */
  auto MakeVisitor() const noexcept {
    return detail::VisitorGenerator<const Derived, PayloadVariant>{static_cast<const Derived*>(this)};
  }

  StaticComponent() noexcept = default;
  ~StaticComponent() = default;

  // Non-copyable, non-movable (same as Component)
  StaticComponent(const StaticComponent&) = delete;
  StaticComponent& operator=(const StaticComponent&) = delete;
  StaticComponent(StaticComponent&&) = delete;
  StaticComponent& operator=(StaticComponent&&) = delete;
};

}  // namespace mccc

#endif  // MCCC_STATIC_COMPONENT_HPP_
