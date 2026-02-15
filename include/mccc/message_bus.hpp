/**
 * @file message_bus.hpp
 * @brief High-performance message bus with priority-based admission control
 *
 * Features:
 * - Lock-free MPSC (Multi-Producer Single-Consumer) queue
 * - Priority-based admission control (HIGH/MEDIUM/LOW)
 * - Zero heap allocation in hot path (envelope embedded in ring buffer)
 * - MISRA C++ compliant
 * - Configurable cache-line alignment (MCCC_SINGLE_CORE=0 enables alignment)
 * - Configurable queue depth (MCCC_QUEUE_DEPTH)
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 *
 * Usage:
 *   struct SensorData { float temp; };
 *   struct MotorCmd   { int speed; };
 *   using MyPayload = std::variant<SensorData, MotorCmd>;
 *   using MyBus = mccc::AsyncBus<MyPayload>;
 *
 *   MyBus::Instance().Subscribe<SensorData>([](const auto& env) { ... });
 *   MyBus::Instance().Publish(SensorData{25.0f}, 1);
 */

#ifndef MCCC_MESSAGE_BUS_HPP_
#define MCCC_MESSAGE_BUS_HPP_

#include "mccc/protocol.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <new>
#include <type_traits>

namespace mccc {

// ============================================================================
// Compile-time Configuration
// ============================================================================

#ifndef MCCC_QUEUE_DEPTH
#define MCCC_QUEUE_DEPTH 131072U
#endif

#ifndef MCCC_CACHELINE_SIZE
#define MCCC_CACHELINE_SIZE 64U
#endif

#ifndef MCCC_SINGLE_PRODUCER
#define MCCC_SINGLE_PRODUCER 0
#endif

#ifndef MCCC_SINGLE_CORE
#define MCCC_SINGLE_CORE 0
#endif

// ---- MCCC_SINGLE_CORE safety guard ----
// MCCC_SINGLE_CORE=1 replaces hardware memory barriers (DMB/MFENCE) with
// compiler-only signal fences (std::atomic_signal_fence). This is ONLY safe on:
//   - Single-core MCUs (Cortex-M0/M3/M4/M7, single-core RISC-V)
//   - Bare-metal or single-core RTOS (FreeRTOS on 1 core, etc.)
// It is UNSAFE on multi-core SMP systems (ARM-Linux, multi-core Cortex-A, x86 SMP).
// To enable, you MUST also define MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1 to confirm
// you understand the implications.
#if MCCC_SINGLE_CORE
  #if !defined(MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE) || !MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE
    #error "MCCC_SINGLE_CORE=1 disables hardware memory barriers. " \
           "This is ONLY safe on single-core MCUs (Cortex-M, bare-metal RTOS). " \
           "Define MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1 to confirm."
  #endif
#endif

#ifndef MCCC_MAX_MESSAGE_TYPES
#define MCCC_MAX_MESSAGE_TYPES 8U
#endif

#ifndef MCCC_MAX_CALLBACKS_PER_TYPE
#define MCCC_MAX_CALLBACKS_PER_TYPE 16U
#endif

// Single-core mode: disable cache-line alignment (no false sharing concern) + relaxed memory ordering
#if MCCC_SINGLE_CORE
#define MCCC_ALIGN_CACHELINE
#define MCCC_MO_ACQUIRE  std::memory_order_relaxed
#define MCCC_MO_RELEASE  std::memory_order_relaxed
#define MCCC_MO_ACQ_REL  std::memory_order_relaxed
#else
#define MCCC_ALIGN_CACHELINE alignas(MCCC_CACHELINE_SIZE)
#define MCCC_MO_ACQUIRE  std::memory_order_acquire
#define MCCC_MO_RELEASE  std::memory_order_release
#define MCCC_MO_ACQ_REL  std::memory_order_acq_rel
#endif

namespace detail {
inline void AcquireFence() noexcept {
#if MCCC_SINGLE_CORE
    std::atomic_signal_fence(std::memory_order_acquire);
#endif
}
inline void ReleaseFence() noexcept {
#if MCCC_SINGLE_CORE
    std::atomic_signal_fence(std::memory_order_release);
#endif
}
}  // namespace detail

// ============================================================================
// Template Helpers
// ============================================================================

/**
 * @brief Compile-time variant index calculation (C++14 recursive template).
 */
namespace detail {

template <typename T, size_t I, typename Variant>
struct VariantIndexImpl;

template <typename T, size_t I>
struct VariantIndexImpl<T, I, std::variant<>> {
  static constexpr size_t value = static_cast<size_t>(-1);
};

template <typename T, size_t I, typename First, typename... Rest>
struct VariantIndexImpl<T, I, std::variant<First, Rest...>> {
  static constexpr size_t value =
      std::is_same<T, First>::value ? I : VariantIndexImpl<T, I + 1U, std::variant<Rest...>>::value;
};

}  // namespace detail

template <typename T, typename Variant>
struct VariantIndex;

template <typename T, typename... Types>
struct VariantIndex<T, std::variant<Types...>> {
  static constexpr size_t value = detail::VariantIndexImpl<T, 0U, std::variant<Types...>>::value;
  static_assert(value != static_cast<size_t>(-1), "Type not found in PayloadVariant");
};

// ============================================================================
// Bus Error Types
// ============================================================================

enum class BusError : uint8_t {
  QUEUE_FULL = 0U,
  INVALID_MESSAGE = 1U,
  PROCESSING_ERROR = 2U,
  OVERFLOW_DETECTED = 3U
};

using ErrorCallback = void (*)(BusError, uint64_t);

// ============================================================================
// Statistics
// ============================================================================

struct MCCC_ALIGN_CACHELINE BusStatistics {
  std::atomic<uint64_t> messages_published{0U};
  std::atomic<uint64_t> messages_dropped{0U};
  std::atomic<uint64_t> messages_processed{0U};
  std::atomic<uint64_t> processing_errors{0U};

  std::atomic<uint64_t> high_priority_published{0U};
  std::atomic<uint64_t> medium_priority_published{0U};
  std::atomic<uint64_t> low_priority_published{0U};

  std::atomic<uint64_t> high_priority_dropped{0U};
  std::atomic<uint64_t> medium_priority_dropped{0U};
  std::atomic<uint64_t> low_priority_dropped{0U};

  std::atomic<uint64_t> admission_recheck_count{0U};
  std::atomic<uint64_t> stale_cache_depth_delta{0U};

  void Reset() noexcept {
    messages_published.store(0U, std::memory_order_relaxed);
    messages_dropped.store(0U, std::memory_order_relaxed);
    messages_processed.store(0U, std::memory_order_relaxed);
    processing_errors.store(0U, std::memory_order_relaxed);
    high_priority_published.store(0U, std::memory_order_relaxed);
    medium_priority_published.store(0U, std::memory_order_relaxed);
    low_priority_published.store(0U, std::memory_order_relaxed);
    high_priority_dropped.store(0U, std::memory_order_relaxed);
    medium_priority_dropped.store(0U, std::memory_order_relaxed);
    low_priority_dropped.store(0U, std::memory_order_relaxed);
    admission_recheck_count.store(0U, std::memory_order_relaxed);
    stale_cache_depth_delta.store(0U, std::memory_order_relaxed);
  }
};

struct BusStatisticsSnapshot {
  uint64_t messages_published;
  uint64_t messages_dropped;
  uint64_t messages_processed;
  uint64_t processing_errors;
  uint64_t high_priority_published;
  uint64_t medium_priority_published;
  uint64_t low_priority_published;
  uint64_t high_priority_dropped;
  uint64_t medium_priority_dropped;
  uint64_t low_priority_dropped;
  uint64_t admission_recheck_count;
  uint64_t stale_cache_depth_delta;
};

enum class BackpressureLevel : uint8_t {
  NORMAL = 0U,   /**< < 75% full */
  WARNING = 1U,  /**< 75-90% full */
  CRITICAL = 2U, /**< 90-100% full */
  FULL = 3U      /**< 100% full */
};

// ============================================================================
// Subscription Handle
// ============================================================================

struct SubscriptionHandle {
  size_t type_index;  /**< Message type index */
  size_t callback_id; /**< Callback ID within type */
};

// ============================================================================
// AsyncBus<PayloadVariant>
// ============================================================================

/**
 * @brief Lock-free MPSC message bus with priority admission control.
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 */
template <typename PayloadVariant>
class AsyncBus {
 public:
  using EnvelopeType = MessageEnvelope<PayloadVariant>;

  enum class PerformanceMode : uint8_t {
    FULL_FEATURED = 0U,
    BARE_METAL = 1U,
    NO_STATS = 2U
  };

  static constexpr uint32_t MAX_QUEUE_DEPTH = static_cast<uint32_t>(MCCC_QUEUE_DEPTH);
  static constexpr uint32_t BATCH_PROCESS_SIZE = 1024U;
  static constexpr uint64_t MSG_ID_WRAP_THRESHOLD = std::numeric_limits<uint64_t>::max() - 10000U;

  static constexpr uint32_t LOW_PRIORITY_THRESHOLD = (MAX_QUEUE_DEPTH * 60U) / 100U;
  static constexpr uint32_t MEDIUM_PRIORITY_THRESHOLD = (MAX_QUEUE_DEPTH * 80U) / 100U;
  static constexpr uint32_t HIGH_PRIORITY_THRESHOLD = (MAX_QUEUE_DEPTH * 99U) / 100U;

  static constexpr uint32_t BACKPRESSURE_WARNING_THRESHOLD = (MAX_QUEUE_DEPTH * 75U) / 100U;
  static constexpr uint32_t BACKPRESSURE_CRITICAL_THRESHOLD = (MAX_QUEUE_DEPTH * 90U) / 100U;

  using CallbackType = std::function<void(const EnvelopeType&)>;

  static AsyncBus& Instance() noexcept {
    static AsyncBus instance;
    return instance;
  }

  void SetErrorCallback(ErrorCallback callback) noexcept {
    error_callback_.store(callback, std::memory_order_release);
  }

  BusStatisticsSnapshot GetStatistics() const noexcept {
    return BusStatisticsSnapshot{
        stats_.messages_published.load(std::memory_order_relaxed),
        stats_.messages_dropped.load(std::memory_order_relaxed),
        stats_.messages_processed.load(std::memory_order_relaxed),
        stats_.processing_errors.load(std::memory_order_relaxed),
        stats_.high_priority_published.load(std::memory_order_relaxed),
        stats_.medium_priority_published.load(std::memory_order_relaxed),
        stats_.low_priority_published.load(std::memory_order_relaxed),
        stats_.high_priority_dropped.load(std::memory_order_relaxed),
        stats_.medium_priority_dropped.load(std::memory_order_relaxed),
        stats_.low_priority_dropped.load(std::memory_order_relaxed),
        stats_.admission_recheck_count.load(std::memory_order_relaxed),
        stats_.stale_cache_depth_delta.load(std::memory_order_relaxed)};
  }

  void ResetStatistics() noexcept { stats_.Reset(); }

  void SetPerformanceMode(PerformanceMode mode) noexcept {
    performance_mode_.store(mode, std::memory_order_relaxed);
  }

  // ======================== Publish API ========================

  bool Publish(PayloadVariant&& payload, uint32_t sender_id) noexcept {
    return PublishInternal(std::move(payload), sender_id, GetTimestampUs(), MessagePriority::MEDIUM);
  }

  bool PublishWithPriority(PayloadVariant&& payload, uint32_t sender_id, MessagePriority priority) noexcept {
    return PublishInternal(std::move(payload), sender_id, GetTimestampUs(), priority);
  }

  bool PublishFast(PayloadVariant&& payload, uint32_t sender_id, uint64_t timestamp_us) noexcept {
    return PublishInternal(std::move(payload), sender_id, timestamp_us, MessagePriority::MEDIUM);
  }

  // ======================== Subscribe API ========================

  template <typename T, typename Func>
  SubscriptionHandle Subscribe(Func&& func) {
    constexpr size_t type_idx = VariantIndex<T, PayloadVariant>::value;
    static_assert(type_idx < MCCC_MAX_MESSAGE_TYPES, "Type index exceeds MCCC_MAX_MESSAGE_TYPES");

    std::unique_lock<std::shared_mutex> lock(callback_mutex_);

    CallbackSlot& slot = callback_table_[type_idx];
    size_t callback_id = next_callback_id_++;

    for (uint32_t i = 0U; i < MCCC_MAX_CALLBACKS_PER_TYPE; ++i) {
      if (!slot.entries[i].active) {
        slot.entries[i].id = callback_id;
        slot.entries[i].callback = CallbackType(std::forward<Func>(func));
        slot.entries[i].active = true;
        ++slot.count;
        return SubscriptionHandle{type_idx, callback_id};
      }
    }

    return SubscriptionHandle{type_idx, static_cast<size_t>(-1)};
  }

  bool Unsubscribe(const SubscriptionHandle& handle) noexcept {
    if (handle.type_index >= MCCC_MAX_MESSAGE_TYPES) {
      return false;
    }

    std::unique_lock<std::shared_mutex> lock(callback_mutex_);

    CallbackSlot& slot = callback_table_[handle.type_index];
    for (uint32_t i = 0U; i < MCCC_MAX_CALLBACKS_PER_TYPE; ++i) {
      if (slot.entries[i].active && (slot.entries[i].id == handle.callback_id)) {
        slot.entries[i].active = false;
        slot.entries[i].callback = nullptr;
        --slot.count;
        return true;
      }
    }
    return false;
  }

  // ======================== Processing API ========================

  uint32_t ProcessBatch() noexcept {
    uint32_t processed = 0U;
    uint32_t cons_pos = consumer_pos_.load(std::memory_order_relaxed);
    const PerformanceMode mode = performance_mode_.load(std::memory_order_relaxed);
    const bool bare_metal = (mode == PerformanceMode::BARE_METAL);
    const bool no_stats = bare_metal || (mode == PerformanceMode::NO_STATS);
    for (uint32_t i = 0U; i < BATCH_PROCESS_SIZE; ++i) {
      if (!ProcessOneInBatch(cons_pos, bare_metal)) {
        break;
      }
      ++cons_pos;
      ++processed;
    }
    if (processed > 0U) {
      consumer_pos_.store(cons_pos, std::memory_order_relaxed);
      if (!no_stats) {
        stats_.messages_processed.fetch_add(processed, std::memory_order_relaxed);
      }
    }
    return processed;
  }

  // ======================== Queue Status API ========================

  uint32_t QueueDepth() const noexcept {
    uint32_t prod = producer_pos_.load(std::memory_order_acquire);
    uint32_t cons = consumer_pos_.load(std::memory_order_acquire);
    return prod - cons;
  }

  uint32_t QueueUtilizationPercent() const noexcept {
    return (QueueDepth() * 100U) / MAX_QUEUE_DEPTH;
  }

  BackpressureLevel GetBackpressureLevel() const noexcept {
    uint32_t depth = QueueDepth();
    if (depth >= MAX_QUEUE_DEPTH) {
      return BackpressureLevel::FULL;
    }
    if (depth >= BACKPRESSURE_CRITICAL_THRESHOLD) {
      return BackpressureLevel::CRITICAL;
    }
    if (depth >= BACKPRESSURE_WARNING_THRESHOLD) {
      return BackpressureLevel::WARNING;
    }
    return BackpressureLevel::NORMAL;
  }

 private:
  // ======================== Ring Buffer Node ========================

  struct MCCC_ALIGN_CACHELINE RingBufferNode {
    std::atomic<uint32_t> sequence{0U};
    EnvelopeType envelope;
  };

  static constexpr uint32_t BUFFER_SIZE = static_cast<uint32_t>(MCCC_QUEUE_DEPTH);
  static constexpr uint32_t BUFFER_MASK = BUFFER_SIZE - 1U;

  static_assert((BUFFER_SIZE & (BUFFER_SIZE - 1U)) == 0U, "BUFFER_SIZE must be power of 2");

  struct CallbackEntry {
    size_t id{0U};
    CallbackType callback{nullptr};
    bool active{false};
  };

  struct CallbackSlot {
    std::array<CallbackEntry, MCCC_MAX_CALLBACKS_PER_TYPE> entries{};
    uint32_t count{0U};
  };

  bool ProcessOne() noexcept {
    uint32_t cons_pos = consumer_pos_.load(std::memory_order_relaxed);
    return ProcessOneAtPos(cons_pos);
  }

  bool ProcessOneAtPos(uint32_t cons_pos) noexcept {
    RingBufferNode& node = ring_buffer_[cons_pos & BUFFER_MASK];

    uint32_t expected_seq = cons_pos + 1U;
    uint32_t seq = node.sequence.load(MCCC_MO_ACQUIRE);
    detail::AcquireFence();

    if (seq != expected_seq) {
      return false;
    }

    DispatchMessage(node.envelope);
    stats_.messages_processed.fetch_add(1U, std::memory_order_relaxed);

    detail::ReleaseFence();
    node.sequence.store(cons_pos + BUFFER_SIZE, MCCC_MO_RELEASE);
    consumer_pos_.store(cons_pos + 1U, std::memory_order_relaxed);

    return true;
  }

  bool ProcessOneInBatch(uint32_t cons_pos, bool bare_metal) noexcept {
    RingBufferNode& node = ring_buffer_[cons_pos & BUFFER_MASK];

    uint32_t expected_seq = cons_pos + 1U;
    uint32_t seq = node.sequence.load(MCCC_MO_ACQUIRE);
    detail::AcquireFence();

    if (seq != expected_seq) {
      return false;
    }

    if (bare_metal) {
      DispatchMessageBareMetal(node.envelope);
    } else {
      DispatchMessage(node.envelope);
    }

    detail::ReleaseFence();
    node.sequence.store(cons_pos + BUFFER_SIZE, MCCC_MO_RELEASE);

    return true;
  }

  AsyncBus() noexcept
      : producer_pos_(0U),
        cached_consumer_pos_(0U),
        consumer_pos_(0U),
        next_msg_id_(1U),
        next_callback_id_(1U),
        stats_() {
    for (uint32_t i = 0U; i < BUFFER_SIZE; ++i) {
      ring_buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  ~AsyncBus() = default;
  AsyncBus(const AsyncBus&) = delete;
  AsyncBus& operator=(const AsyncBus&) = delete;
  AsyncBus(AsyncBus&&) = delete;
  AsyncBus& operator=(AsyncBus&&) = delete;

  uint32_t GetThresholdForPriority(MessagePriority priority) const noexcept {
    switch (priority) {
      case MessagePriority::HIGH:
        return HIGH_PRIORITY_THRESHOLD;
      case MessagePriority::MEDIUM:
        return MEDIUM_PRIORITY_THRESHOLD;
      case MessagePriority::LOW:
      default:
        return LOW_PRIORITY_THRESHOLD;
    }
  }

  bool PublishInternal(PayloadVariant&& payload, uint32_t sender_id, uint64_t timestamp_us,
                       MessagePriority priority) noexcept {
    const PerformanceMode mode = performance_mode_.load(std::memory_order_relaxed);
    const bool bare_metal = (mode == PerformanceMode::BARE_METAL);
    const bool no_stats = bare_metal || (mode == PerformanceMode::NO_STATS);

    uint64_t msg_id = next_msg_id_.load(std::memory_order_relaxed);
    if (msg_id >= MSG_ID_WRAP_THRESHOLD) {
      if (!no_stats) {
        ReportError(BusError::OVERFLOW_DETECTED, msg_id);
      }
      return false;
    }

    if (!bare_metal) {
      uint32_t threshold = GetThresholdForPriority(priority);
      uint32_t prod = producer_pos_.load(std::memory_order_relaxed);
      uint32_t cached_cons = cached_consumer_pos_.load(std::memory_order_relaxed);
      uint32_t estimated_depth = prod - cached_cons;
      if (estimated_depth >= threshold) {
        uint32_t real_cons = consumer_pos_.load(MCCC_MO_ACQUIRE);
        cached_consumer_pos_.store(real_cons, std::memory_order_relaxed);
        uint32_t real_depth = prod - real_cons;
        if (!no_stats) {
          stats_.admission_recheck_count.fetch_add(1U, std::memory_order_relaxed);
          if (estimated_depth > real_depth) {
            stats_.stale_cache_depth_delta.fetch_add(
                estimated_depth - real_depth, std::memory_order_relaxed);
          }
        }
        if (real_depth >= threshold) {
          if (!no_stats) {
            stats_.messages_dropped.fetch_add(1U, std::memory_order_relaxed);
            UpdatePriorityDroppedStats(priority);
            ReportError(BusError::QUEUE_FULL, msg_id);
          }
          return false;
        }
      }
    }

    uint32_t prod_pos;
    RingBufferNode* node;

#if MCCC_SINGLE_PRODUCER
    prod_pos = producer_pos_.load(std::memory_order_relaxed);
    node = &ring_buffer_[prod_pos & BUFFER_MASK];

    uint32_t seq = node->sequence.load(MCCC_MO_ACQUIRE);
    detail::AcquireFence();
    if (seq != prod_pos) {
      if (!no_stats) {
        stats_.messages_dropped.fetch_add(1U, std::memory_order_relaxed);
        UpdatePriorityDroppedStats(priority);
        ReportError(BusError::QUEUE_FULL, msg_id);
      }
      return false;
    }
    producer_pos_.store(prod_pos + 1U, std::memory_order_relaxed);
#else
    do {
      prod_pos = producer_pos_.load(std::memory_order_relaxed);
      node = &ring_buffer_[prod_pos & BUFFER_MASK];

      uint32_t seq = node->sequence.load(MCCC_MO_ACQUIRE);
      detail::AcquireFence();
      if (seq != prod_pos) {
        if (!no_stats) {
          stats_.messages_dropped.fetch_add(1U, std::memory_order_relaxed);
          UpdatePriorityDroppedStats(priority);
          ReportError(BusError::QUEUE_FULL, msg_id);
        }
        return false;
      }

    } while (!producer_pos_.compare_exchange_weak(prod_pos, prod_pos + 1U,
                                                   MCCC_MO_ACQ_REL,
                                                   std::memory_order_relaxed));
#endif

    uint64_t assigned_id = next_msg_id_.fetch_add(1U, std::memory_order_relaxed);
    node->envelope.header = MessageHeader{assigned_id, timestamp_us, sender_id, priority};
    node->envelope.payload = std::move(payload);

    detail::ReleaseFence();
    node->sequence.store(prod_pos + 1U, MCCC_MO_RELEASE);

    if (!no_stats) {
      stats_.messages_published.fetch_add(1U, std::memory_order_relaxed);
      UpdatePriorityPublishedStats(priority);
    }
    return true;
  }

  void DispatchMessage(const EnvelopeType& envelope) noexcept {
    size_t type_idx = envelope.payload.index();
    if (type_idx >= MCCC_MAX_MESSAGE_TYPES) {
      return;
    }

    std::shared_lock<std::shared_mutex> lock(callback_mutex_);

    const CallbackSlot& slot = callback_table_[type_idx];
    if (slot.count == 0U) {
      return;
    }

    for (uint32_t i = 0U; i < MCCC_MAX_CALLBACKS_PER_TYPE; ++i) {
      if (slot.entries[i].active) {
        slot.entries[i].callback(envelope);
      }
    }
  }

  void DispatchMessageBareMetal(const EnvelopeType& envelope) noexcept {
    size_t type_idx = envelope.payload.index();
    if (type_idx >= MCCC_MAX_MESSAGE_TYPES) {
      return;
    }

    const CallbackSlot& slot = callback_table_[type_idx];
    if (slot.count == 0U) {
      return;
    }

    for (uint32_t i = 0U; i < MCCC_MAX_CALLBACKS_PER_TYPE; ++i) {
      if (slot.entries[i].active) {
        slot.entries[i].callback(envelope);
      }
    }
  }

  void UpdatePriorityPublishedStats(MessagePriority priority) noexcept {
    switch (priority) {
      case MessagePriority::HIGH:
        stats_.high_priority_published.fetch_add(1U, std::memory_order_relaxed);
        break;
      case MessagePriority::MEDIUM:
        stats_.medium_priority_published.fetch_add(1U, std::memory_order_relaxed);
        break;
      case MessagePriority::LOW:
        stats_.low_priority_published.fetch_add(1U, std::memory_order_relaxed);
        break;
      default:
        break;
    }
  }

  void UpdatePriorityDroppedStats(MessagePriority priority) noexcept {
    switch (priority) {
      case MessagePriority::HIGH:
        stats_.high_priority_dropped.fetch_add(1U, std::memory_order_relaxed);
        break;
      case MessagePriority::MEDIUM:
        stats_.medium_priority_dropped.fetch_add(1U, std::memory_order_relaxed);
        break;
      case MessagePriority::LOW:
        stats_.low_priority_dropped.fetch_add(1U, std::memory_order_relaxed);
        break;
      default:
        break;
    }
  }

  void ReportError(BusError error, uint64_t msg_id) const noexcept {
    ErrorCallback cb = error_callback_.load(std::memory_order_acquire);
    if (cb != nullptr) {
      cb(error, msg_id);
    }
  }

  static uint64_t GetTimestampUs() noexcept {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(us.count());
  }

  MCCC_ALIGN_CACHELINE std::array<RingBufferNode, BUFFER_SIZE> ring_buffer_;
  MCCC_ALIGN_CACHELINE std::atomic<uint32_t> producer_pos_;
  std::atomic<uint32_t> cached_consumer_pos_{0U};
  MCCC_ALIGN_CACHELINE std::atomic<uint32_t> consumer_pos_;
  MCCC_ALIGN_CACHELINE std::atomic<uint64_t> next_msg_id_;
  size_t next_callback_id_;
  MCCC_ALIGN_CACHELINE BusStatistics stats_;
  std::array<CallbackSlot, MCCC_MAX_MESSAGE_TYPES> callback_table_;
  mutable std::shared_mutex callback_mutex_;
  std::atomic<ErrorCallback> error_callback_{nullptr};
  std::atomic<PerformanceMode> performance_mode_{PerformanceMode::FULL_FEATURED};
};

}  // namespace mccc

#endif  // MCCC_MESSAGE_BUS_HPP_
