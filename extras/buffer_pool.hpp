/**
 * @file buffer_pool.hpp
 * @brief DMA buffer pool with zero-copy token management (lock-free, sharded)
 *
 * Thread Safety:
 * - Uses tagged pointer to solve ABA problem
 * - Uses sharding to reduce contention on free list head
 *
 * MISRA C++ Compliance:
 * - Rule 18-4-1: No heap allocation in Borrow() hot path
 *   (DMABufferReleaser eliminated, replaced by inline function pointer)
 *
 * Compile-time Configuration:
 * - STREAMING_DMA_ALIGNMENT: Buffer alignment in bytes (default: 64)
 *   Override: -DSTREAMING_DMA_ALIGNMENT=0 (disable alignment on MCUs)
 */

#ifndef BUFFER_POOL_HPP_
#define BUFFER_POOL_HPP_

#include "data_token.hpp"

#include <cstdlib>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace streaming {

/**
 * @brief DMA/cache-line alignment for buffer pool allocations.
 *
 * Default: 64 bytes (x86/ARM Cortex-A cache line size).
 * Set to 0 on single-core MCUs without cache to save RAM.
 * Override at compile time: -DSTREAMING_DMA_ALIGNMENT=32
 */
#ifndef STREAMING_DMA_ALIGNMENT
#define STREAMING_DMA_ALIGNMENT 64U
#endif

/**
 * @brief Tagged pointer to solve ABA problem
 */
struct TaggedIndex {
  uint32_t index;
  uint32_t version;

  TaggedIndex() noexcept : index(0U), version(0U) {}
  TaggedIndex(uint32_t idx, uint32_t ver) noexcept : index(idx), version(ver) {}
};

/**
 * @brief Single shard of the buffer pool (cache-line aligned if enabled)
 */
#if STREAMING_DMA_ALIGNMENT > 0
struct alignas(STREAMING_DMA_ALIGNMENT) BufferPoolShard {
#else
struct BufferPoolShard {
#endif
  std::atomic<uint64_t> free_head;  // Tagged pointer (index + version)
  std::atomic<uint32_t> available_count;

  BufferPoolShard() noexcept : free_head(0), available_count(0) {}
};

/**
 * @brief DMA-aligned buffer pool for zero-copy data flow
 *
 * Features:
 * - Lock-free free list with O(1) borrow/return
 * - Tagged pointer for ABA safety
 * - Sharding to reduce contention (each CPU core has preferred shard)
 * - Cache line aligned to avoid false sharing
 * - Zero heap allocation in Borrow() hot path (function pointer releaser)
 */
class DMABufferPool {
 public:
  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFU;
  static constexpr uint32_t kDefaultShardCount = 4U;

  DMABufferPool(uint32_t buffer_size, uint32_t buffer_count, uint32_t shard_count = kDefaultShardCount);
  ~DMABufferPool();

  // Non-copyable, non-movable
  DMABufferPool(const DMABufferPool&) = delete;
  DMABufferPool& operator=(const DMABufferPool&) = delete;

  /**
   * @brief Borrow a buffer from the pool (lock-free, sharded, zero heap alloc)
   * @return DataToken with borrowed buffer, or invalid token if pool is empty
   */
  DataToken Borrow();

  /**
   * @brief Return a buffer to the pool by index (lock-free)
   */
  void Return(uint32_t index);

  /**
   * @brief Get total number of buffers
   */
  uint32_t TotalBuffers() const { return buffer_count_; }

  /**
   * @brief Get number of available buffers (approximate)
   */
  uint32_t AvailableBuffers() const;

  /**
   * @brief Get borrow count
   */
  uint64_t BorrowCount() const { return borrow_count_.load(std::memory_order_relaxed); }

  /**
   * @brief Get return count
   */
  uint64_t ReturnCount() const { return return_count_.load(std::memory_order_relaxed); }

 private:
  /**
   * @brief Static release callback for DataToken RAII.
   *
   * Called by DataToken destructor via function pointer.
   * Casts context back to DMABufferPool* and calls Return().
   * No virtual dispatch, no heap allocation.
   */
  static void ReleaseBuffer(void* context, uint32_t index) noexcept {
    auto* pool = static_cast<DMABufferPool*>(context);
    if (pool != nullptr) {
      pool->Return(index);
    }
  }

  // Get shard index for current thread (round-robin with thread hint)
  uint32_t GetShardIndex() const noexcept {
    // Use thread ID hash for shard selection
    static thread_local uint32_t cached_shard =
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return cached_shard % shard_count_;
  }

  // Get shard for a buffer index (determined at allocation time)
  uint32_t GetBufferShard(uint32_t buffer_index) const noexcept { return buffer_index % shard_count_; }

  // Try to borrow from a specific shard
  DataToken TryBorrowFromShard(uint32_t shard_idx);

  // Helper functions for tagged pointer
  static uint64_t PackTaggedIndex(TaggedIndex ti) noexcept {
    return (static_cast<uint64_t>(ti.version) << 32U) | ti.index;
  }

  static TaggedIndex UnpackTaggedIndex(uint64_t packed) noexcept {
    return TaggedIndex(static_cast<uint32_t>(packed & 0xFFFFFFFFU), static_cast<uint32_t>(packed >> 32U));
  }

  // Buffer storage
  std::vector<uint8_t*> buffers_;
  uint32_t buffer_size_;
  uint32_t buffer_count_;
  uint32_t shard_count_;

  // Per-buffer next pointer for free list
  std::unique_ptr<std::atomic<uint32_t>[]> next_free_;

  // Sharded free list heads (cache-line aligned)
  std::unique_ptr<BufferPoolShard[]> shards_;

  // Global statistics (cache line aligned)
  alignas(64) std::atomic<uint64_t> borrow_count_;
  alignas(64) std::atomic<uint64_t> return_count_;
};

} /* namespace streaming */

#endif /* BUFFER_POOL_HPP_ */
