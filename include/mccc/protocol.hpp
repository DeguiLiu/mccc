/**
 * @file protocol.hpp
 * @brief MCCC protocol definitions: fixed containers, message envelope, priority.
 *
 * MISRA C++ Compliance:
 * - Rule 0-1-1: All code shall conform to ISO/IEC 14882:2014
 * - No heap allocation in protocol data types (hot path)
 * - Fixed-width integer types throughout
 */

#ifndef MCCC_PROTOCOL_HPP_
#define MCCC_PROTOCOL_HPP_

#include <cstdint>
#include <cstring>

#include <string>
#include <type_traits>
#include <variant>

namespace mccc {

// ============================================================================
// FixedString<N> - Stack-allocated fixed-capacity string (inspired by iceoryx)
// ============================================================================

/**
 * @brief Tag type to explicitly acknowledge truncation at call site.
 *
 * Inspired by iceoryx TruncateToCapacity_t pattern.
 * Forces the caller to be explicit about possible data loss.
 */
struct TruncateToCapacity_t {};
static constexpr TruncateToCapacity_t TruncateToCapacity{};

/**
 * @brief Fixed-capacity, stack-allocated, null-terminated string.
 *
 * Inspired by iceoryx iox::string<N>. Provides safety comparable to
 * std::string without heap allocation. Suitable for MISRA C++ and
 * real-time / embedded systems.
 *
 * @tparam Capacity Maximum number of characters (excluding null terminator)
 */
template <uint32_t Capacity>
class FixedString {
  static_assert(Capacity > 0U, "FixedString capacity must be > 0");

 public:
  /** @brief Default constructor - empty string */
  constexpr FixedString() noexcept : buf_{'\0'}, size_(0U) {}

  /**
   * @brief Construct from string literal (compile-time size check).
   * @tparam N Array size including null terminator
   * @param str String literal
   *
   * Static assertion prevents overflow at compile time.
   */
  template <uint32_t N, typename = typename std::enable_if<(N <= Capacity + 1U)>::type>
  FixedString(const char (&str)[N]) noexcept : size_(N - 1U) {  // NOLINT: implicit conversion intended
    static_assert(N > 0U, "String literal must include null terminator");
    static_assert(N - 1U <= Capacity, "String literal exceeds FixedString capacity");
    (void)std::memcpy(buf_, str, N);
  }

  /**
   * @brief Construct from C-string with explicit truncation acknowledgment.
   * @param tag TruncateToCapacity tag
   * @param str Source C-string (must be null-terminated)
   */
  FixedString(TruncateToCapacity_t /*tag*/, const char* str) noexcept : size_(0U) {
    if (str != nullptr) {
      uint32_t i = 0U;
      while ((i < Capacity) && (str[i] != '\0')) {
        buf_[i] = str[i];
        ++i;
      }
      size_ = i;
    }
    buf_[size_] = '\0';
  }

  /**
   * @brief Construct from C-string with explicit length and truncation.
   * @param tag TruncateToCapacity tag
   * @param str Source buffer
   * @param count Number of characters to copy
   */
  FixedString(TruncateToCapacity_t /*tag*/, const char* str, uint32_t count) noexcept : size_(0U) {
    if (str != nullptr) {
      size_ = (count < Capacity) ? count : Capacity;
      (void)std::memcpy(buf_, str, size_);
    }
    buf_[size_] = '\0';
  }

  /**
   * @brief Construct from std::string with truncation.
   * @param tag TruncateToCapacity tag
   * @param str Source std::string
   */
  FixedString(TruncateToCapacity_t /*tag*/, const std::string& str) noexcept : size_(0U) {
    size_ = (static_cast<uint32_t>(str.size()) < Capacity) ? static_cast<uint32_t>(str.size()) : Capacity;
    (void)std::memcpy(buf_, str.c_str(), size_);
    buf_[size_] = '\0';
  }

  /** @brief Get null-terminated C string */
  constexpr const char* c_str() const noexcept { return buf_; }

  /** @brief Get current string length */
  constexpr uint32_t size() const noexcept { return size_; }

  /** @brief Get maximum capacity */
  static constexpr uint32_t capacity() noexcept { return Capacity; }

  /** @brief Check if empty */
  constexpr bool empty() const noexcept { return size_ == 0U; }

  /** @brief Equality comparison with another FixedString */
  template <uint32_t N>
  bool operator==(const FixedString<N>& rhs) const noexcept {
    if (size_ != rhs.size()) {
      return false;
    }
    return std::memcmp(buf_, rhs.c_str(), size_) == 0;
  }

  /** @brief Inequality comparison */
  template <uint32_t N>
  bool operator!=(const FixedString<N>& rhs) const noexcept {
    return !(*this == rhs);
  }

  /** @brief Equality comparison with string literal */
  template <uint32_t N>
  bool operator==(const char (&str)[N]) const noexcept {
    if (size_ != (N - 1U)) {
      return false;
    }
    return std::memcmp(buf_, str, size_) == 0;
  }

  /** @brief Assign from string literal (compile-time checked) */
  template <uint32_t N, typename = typename std::enable_if<(N <= Capacity + 1U)>::type>
  FixedString& operator=(const char (&str)[N]) noexcept {
    static_assert(N - 1U <= Capacity, "String literal exceeds FixedString capacity");
    size_ = N - 1U;
    (void)std::memcpy(buf_, str, N);
    return *this;
  }

  /** @brief Truncating assign from C-string */
  FixedString& assign(TruncateToCapacity_t /*tag*/, const char* str) noexcept {
    size_ = 0U;
    if (str != nullptr) {
      uint32_t i = 0U;
      while ((i < Capacity) && (str[i] != '\0')) {
        buf_[i] = str[i];
        ++i;
      }
      size_ = i;
    }
    buf_[size_] = '\0';
    return *this;
  }

  /** @brief Clear string */
  void clear() noexcept {
    size_ = 0U;
    buf_[0] = '\0';
  }

 private:
  char buf_[Capacity + 1U];  /**< Buffer including null terminator */
  uint32_t size_;             /**< Current string length */
};

// ============================================================================
// FixedVector<T, N> - Stack-allocated fixed-capacity vector (inspired by iceoryx)
// ============================================================================

/**
 * @brief Fixed-capacity, stack-allocated vector with no heap allocation.
 *
 * @tparam T Element type
 * @tparam Capacity Maximum number of elements
 */
template <typename T, uint32_t Capacity>
class FixedVector final {
  static_assert(Capacity > 0U, "FixedVector capacity must be > 0");

 public:
  using value_type = T;
  using size_type = uint32_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  /** @brief Default constructor - empty vector */
  FixedVector() noexcept : size_(0U) {}

  /** @brief Destructor - explicitly destroys all elements */
  ~FixedVector() noexcept { clear(); }

  /** @brief Copy constructor */
  FixedVector(const FixedVector& other) noexcept : size_(0U) {
    for (uint32_t i = 0U; i < other.size_; ++i) {
      (void)push_back(other.at_unchecked(i));
    }
  }

  /** @brief Copy assignment */
  FixedVector& operator=(const FixedVector& other) noexcept {
    if (this != &other) {
      clear();
      for (uint32_t i = 0U; i < other.size_; ++i) {
        (void)push_back(other.at_unchecked(i));
      }
    }
    return *this;
  }

  /** @brief Move constructor */
  FixedVector(FixedVector&& other) noexcept : size_(0U) {
    for (uint32_t i = 0U; i < other.size_; ++i) {
      (void)emplace_back(std::move(other.at_unchecked(i)));
    }
    other.clear();
  }

  /** @brief Move assignment */
  FixedVector& operator=(FixedVector&& other) noexcept {
    if (this != &other) {
      clear();
      for (uint32_t i = 0U; i < other.size_; ++i) {
        (void)emplace_back(std::move(other.at_unchecked(i)));
      }
      other.clear();
    }
    return *this;
  }

  // ======================== Element Access ========================

  reference operator[](uint32_t index) noexcept { return at_unchecked(index); }
  const_reference operator[](uint32_t index) const noexcept { return at_unchecked(index); }

  reference front() noexcept { return at_unchecked(0U); }
  const_reference front() const noexcept { return at_unchecked(0U); }

  reference back() noexcept { return at_unchecked(size_ - 1U); }
  const_reference back() const noexcept { return at_unchecked(size_ - 1U); }

  pointer data() noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<T*>(storage_);
  }
  const_pointer data() const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const T*>(storage_);
  }

  // ======================== Iterators ========================

  iterator begin() noexcept { return data(); }
  const_iterator begin() const noexcept { return data(); }
  iterator end() noexcept { return data() + size_; }
  const_iterator end() const noexcept { return data() + size_; }

  // ======================== Capacity ========================

  bool empty() const noexcept { return size_ == 0U; }
  uint32_t size() const noexcept { return size_; }
  static constexpr uint32_t capacity() noexcept { return Capacity; }
  bool full() const noexcept { return size_ >= Capacity; }

  // ======================== Modifiers ========================

  bool push_back(const T& value) noexcept { return emplace_back(value); }
  bool push_back(T&& value) noexcept { return emplace_back(std::move(value)); }

  template <typename... Args>
  bool emplace_back(Args&&... args) noexcept {
    if (size_ >= Capacity) {
      return false;
    }
    // Placement new works correctly for both trivial and non-trivial types;
    // compilers optimize away the constructor call for trivial types.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    new (&storage_[size_ * sizeof(T)]) T{std::forward<Args>(args)...};
    ++size_;
    return true;
  }

  bool pop_back() noexcept {
    if (size_ == 0U) {
      return false;
    }
    --size_;
    // Destructor call on trivially-destructible T is a no-op; compiler optimizes it away.
    at_unchecked(size_).~T();
    return true;
  }

  bool erase_unordered(uint32_t index) noexcept {
    if (index >= size_) {
      return false;
    }
    --size_;
    if (index != size_) {
      at_unchecked(index) = std::move(at_unchecked(size_));
    }
    at_unchecked(size_).~T();
    return true;
  }

  void clear() noexcept {
    while (size_ > 0U) {
      --size_;
      at_unchecked(size_).~T();
    }
  }

 private:
  reference at_unchecked(uint32_t index) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *(reinterpret_cast<T*>(storage_) + index);
  }

  const_reference at_unchecked(uint32_t index) const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *(reinterpret_cast<const T*>(storage_) + index);
  }

  alignas(T) uint8_t storage_[sizeof(T) * Capacity];
  uint32_t size_;
};

// ============================================================================
// Overloaded pattern for std::visit (C++14 compatible)
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
// Message Priority & Header
// ============================================================================

/**
 * @brief Message priority levels for backpressure control.
 */
enum class MessagePriority : uint8_t {
  LOW = 0U,    /** Dropped when queue >= 60% full */
  MEDIUM = 1U, /** Dropped when queue >= 80% full */
  HIGH = 2U    /** Dropped when queue >= 99% full (highest admission threshold) */
};

/**
 * @brief Message header for tracing and debugging.
 */
struct MessageHeader {
  uint64_t msg_id;          /** Global incremental ID */
  uint64_t timestamp_us;    /** Microsecond timestamp */
  uint32_t sender_id;       /** Sender identifier */
  MessagePriority priority; /** Message priority level */

  MessageHeader() noexcept : msg_id(0U), timestamp_us(0U), sender_id(0U), priority(MessagePriority::MEDIUM) {}
  MessageHeader(uint64_t id, uint64_t ts, uint32_t sender, MessagePriority prio) noexcept
      : msg_id(id), timestamp_us(ts), sender_id(sender), priority(prio) {}
};

// ============================================================================
// Message Envelope (templatized on user-defined PayloadVariant)
// ============================================================================

/**
 * @brief Message envelope (value type, embeddable directly in ring buffer).
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 *
 * Example:
 *   using MyPayload = std::variant<SensorData, MotorCommand, AlarmEvent>;
 *   using MyEnvelope = mccc::MessageEnvelope<MyPayload>;
 */
template <typename PayloadVariant>
struct MessageEnvelope {
  MessageHeader header;
  PayloadVariant payload;

  /** @brief Default constructor for ring buffer pre-allocation */
  MessageEnvelope() noexcept : header(), payload() {}

  explicit MessageEnvelope(const MessageHeader& hdr, PayloadVariant&& pl) noexcept
      : header(hdr), payload(std::move(pl)) {}

  MessageEnvelope(MessageEnvelope&&) noexcept = default;
  MessageEnvelope& operator=(MessageEnvelope&&) noexcept = default;
  MessageEnvelope(const MessageEnvelope&) = default;
  MessageEnvelope& operator=(const MessageEnvelope&) = default;
};

} /* namespace mccc */

#endif /* MCCC_PROTOCOL_HPP_ */
