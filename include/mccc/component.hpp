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
 * @file component.hpp
 * @brief Base component class with safe lifecycle management.
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 */

#ifndef MCCC_COMPONENT_HPP_
#define MCCC_COMPONENT_HPP_

#include "mccc/mccc.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace mccc {

/** Maximum subscriptions per component (compile-time configurable) */
#ifndef MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT
#define MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT 16U
#endif

/**
 * @brief Base component class with safe lifecycle management.
 *
 * Provides shared_from_this capability and automatic subscription cleanup.
 * Uses FixedVector for zero heap allocation in subscription management.
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 */
template <typename PayloadVariant>
class Component : public std::enable_shared_from_this<Component<PayloadVariant>> {
 public:
  using BusType = AsyncBus<PayloadVariant>;
  using EnvelopeType = MessageEnvelope<PayloadVariant>;

  virtual ~Component() {
    for (const auto& handle : handles_) {
      BusType::Instance().Unsubscribe(handle);
    }
  }

  void InitializeComponent() noexcept { /* No-op, can be extended */ }

 protected:
  Component() noexcept = default;

  Component(const Component&) = delete;
  Component& operator=(const Component&) = delete;
  Component(Component&&) = delete;
  Component& operator=(Component&&) = delete;

  /**
   * @brief Safe subscription with automatic weak_ptr wrapping.
   *
   * Uses std::get_if instead of std::get (no exception on type mismatch).
   * Callback signature: (shared_ptr<Component>, const T&, const MessageHeader&)
   */
  template <typename T, typename Func>
  void SubscribeSafe(Func&& callback) noexcept {
    SubscriptionHandle handle = BusType::Instance().template Subscribe<T>(
        [weak_self = std::weak_ptr<Component>(this->shared_from_this()),
         cb = std::forward<Func>(callback)](const EnvelopeType& env) noexcept {
          std::shared_ptr<Component> self = weak_self.lock();
          if (self != nullptr) {
            const T* data = std::get_if<T>(&env.payload);
            if (data != nullptr) {
              cb(self, *data, env.header);
            }
          }
        });

    if (handle.callback_id != static_cast<size_t>(-1)) {
      (void)handles_.push_back(handle);
    }
  }

  /**
   * @brief Subscribe with simple callback (no self pointer).
   *
   * Callback signature: (const T&, const MessageHeader&)
   */
  template <typename T, typename Func>
  void SubscribeSimple(Func&& callback) noexcept {
    SubscriptionHandle handle = BusType::Instance().template Subscribe<T>(
        [cb = std::forward<Func>(callback)](const EnvelopeType& env) noexcept {
          const T* data = std::get_if<T>(&env.payload);
          if (data != nullptr) {
            cb(*data, env.header);
          }
        });

    if (handle.callback_id != static_cast<size_t>(-1)) {
      (void)handles_.push_back(handle);
    }
  }

 private:
  FixedVector<SubscriptionHandle, MCCC_MAX_SUBSCRIPTIONS_PER_COMPONENT> handles_;
};

}  // namespace mccc

#endif  // MCCC_COMPONENT_HPP_
