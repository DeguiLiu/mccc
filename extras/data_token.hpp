/**
 * @file data_token.hpp
 * @brief Zero-copy data token for streaming architecture
 *
 * MISRA C++ Compliance:
 * - Rule 0-1-1: All code shall conform to ISO/IEC 14882:2014
 * - Rule 3-1-1: Header file can be included in multiple translation units
 * - Rule 5-0-13: Explicit boolean comparisons
 * - Rule 12-8-1: Copy operations properly defined
 * - Rule 18-4-1: No dynamic heap allocation (releaser is inline function pointer)
 *
 * Naming convention (Google C++ Style Guide):
 * - Accessors: lowercase (e.g., data(), size())
 * - Regular functions: PascalCase
 */

#ifndef INCLUDE_DATA_TOKEN_HPP_
#define INCLUDE_DATA_TOKEN_HPP_

#include <cstdint>

#include <memory>
#include <utility>

namespace streaming {

/**
 * @brief Release callback type - function pointer for zero-allocation RAII.
 *
 * @param context Opaque pointer to the owning pool (e.g., DMABufferPool*)
 * @param index   Buffer index to return
 *
 * Replaces virtual ITokenReleaser + unique_ptr to eliminate heap allocation
 * on every Borrow(). Function pointer + context is MISRA-friendly and
 * has zero overhead compared to virtual dispatch.
 */
using ReleaseCallback = void (*)(void* context, uint32_t index) noexcept;

/**
 * @brief Zero-copy data token with RAII memory management
 *
 * This class provides zero-copy access to buffer data with automatic
 * resource management through RAII pattern. The release mechanism uses
 * an inline function pointer instead of heap-allocated releaser objects.
 */
class DataToken {
 public:
  /**
   * @brief Default constructor - creates invalid token
   */
  DataToken() noexcept : ptr_(nullptr), len_(0U), timestamp_us_(0U), release_fn_(nullptr), release_ctx_(nullptr), buffer_index_(0U) {}

  /**
   * @brief Construct token with data and release callback
   * @param ptr Pointer to data buffer
   * @param len Size of data in bytes
   * @param timestamp Timestamp in microseconds
   * @param release_fn Function pointer for buffer return
   * @param release_ctx Opaque context (typically pool pointer)
   * @param buffer_index Buffer index in the pool
   */
  DataToken(const uint8_t* ptr, uint32_t len, uint64_t timestamp,
            ReleaseCallback release_fn, void* release_ctx, uint32_t buffer_index) noexcept
      : ptr_(ptr), len_(len), timestamp_us_(timestamp),
        release_fn_(release_fn), release_ctx_(release_ctx), buffer_index_(buffer_index) {}

  // MISRA C++ Rule 12-8-1: Non-copyable
  DataToken(const DataToken&) = delete;
  DataToken& operator=(const DataToken&) = delete;

  /**
   * @brief Move constructor
   * @param other Token to move from
   */
  DataToken(DataToken&& other) noexcept
      : ptr_(other.ptr_), len_(other.len_), timestamp_us_(other.timestamp_us_),
        release_fn_(other.release_fn_), release_ctx_(other.release_ctx_), buffer_index_(other.buffer_index_) {
    other.ptr_ = nullptr;
    other.len_ = 0U;
    other.timestamp_us_ = 0U;
    other.release_fn_ = nullptr;
    other.release_ctx_ = nullptr;
    other.buffer_index_ = 0U;
  }

  /**
   * @brief Move assignment operator
   * @param other Token to move from
   * @return Reference to this
   */
  DataToken& operator=(DataToken&& other) noexcept {
    if (this != &other) {
      // Return current buffer if held
      if (release_fn_ != nullptr) {
        release_fn_(release_ctx_, buffer_index_);
      }
      ptr_ = other.ptr_;
      len_ = other.len_;
      timestamp_us_ = other.timestamp_us_;
      release_fn_ = other.release_fn_;
      release_ctx_ = other.release_ctx_;
      buffer_index_ = other.buffer_index_;
      other.ptr_ = nullptr;
      other.len_ = 0U;
      other.timestamp_us_ = 0U;
      other.release_fn_ = nullptr;
      other.release_ctx_ = nullptr;
      other.buffer_index_ = 0U;
    }
    return *this;
  }

  /**
   * @brief Destructor - returns buffer to pool via function pointer
   */
  ~DataToken() noexcept {
    if (release_fn_ != nullptr) {
      release_fn_(release_ctx_, buffer_index_);
    }
  }

  // --- Accessors (lowercase per Google style) ---

  /** @brief Get pointer to data */
  const uint8_t* data() const noexcept { return ptr_; }

  /** @brief Get size of data in bytes */
  uint32_t size() const noexcept { return len_; }

  /** @brief Get timestamp in microseconds */
  uint64_t timestamp() const noexcept { return timestamp_us_; }

  /** @brief Check if token is valid */
  bool valid() const noexcept { return ptr_ != nullptr; }

  // --- Legacy API (PascalCase, kept for backward compatibility) ---

  const uint8_t* Data() const noexcept { return data(); }
  uint32_t Size() const noexcept { return size(); }
  uint64_t Timestamp() const noexcept { return timestamp(); }
  bool Valid() const noexcept { return valid(); }

 private:
  const uint8_t* ptr_;
  uint32_t len_;
  uint64_t timestamp_us_;
  ReleaseCallback release_fn_;  /**< Function pointer for buffer return (no heap alloc) */
  void* release_ctx_;           /**< Opaque context (e.g., DMABufferPool*) */
  uint32_t buffer_index_;       /**< Buffer index in pool */
};

/**
 * @brief Shared reference to DataToken for multi-consumer scenarios
 */
using TokenRef = std::shared_ptr<DataToken>;

} /* namespace streaming */

#endif /* INCLUDE_DATA_TOKEN_HPP_ */
