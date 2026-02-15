/**
 * @file data_token.cpp
 * @brief Implementation of buffer pool (lock-free, sharded, ABA-safe)
 */

#include "buffer_pool.hpp"

#include <chrono>

namespace streaming {

DMABufferPool::DMABufferPool(uint32_t buffer_size, uint32_t buffer_count, uint32_t shard_count)
    : buffer_size_(buffer_size),
      buffer_count_(buffer_count),
      shard_count_(shard_count > 0U ? shard_count : 1U),
      next_free_(new std::atomic<uint32_t>[buffer_count]),
      shards_(new BufferPoolShard[shard_count_]),
      borrow_count_(0U),
      return_count_(0U) {
  buffers_.reserve(buffer_count);

  // Initialize shards with invalid head
  for (uint32_t s = 0U; s < shard_count_; ++s) {
    shards_[s].free_head.store(PackTaggedIndex(TaggedIndex(kInvalidIndex, 0U)), std::memory_order_relaxed);
    shards_[s].available_count.store(0U, std::memory_order_relaxed);
  }

  // Allocate buffers and distribute to shards
  for (uint32_t i = 0U; i < buffer_count; ++i) {
    // Allocate aligned memory for DMA/cache efficiency
#if STREAMING_DMA_ALIGNMENT > 0
    uint8_t* ptr = static_cast<uint8_t*>(::operator new(buffer_size, std::align_val_t{STREAMING_DMA_ALIGNMENT}));
#else
    uint8_t* ptr = static_cast<uint8_t*>(::operator new(buffer_size));
#endif
    buffers_.push_back(ptr);

    // Determine which shard this buffer belongs to
    uint32_t shard_idx = GetBufferShard(i);
    BufferPoolShard& shard = shards_[shard_idx];

    // Push to shard's free list
    uint64_t old_head_packed = shard.free_head.load(std::memory_order_relaxed);
    TaggedIndex old_head = UnpackTaggedIndex(old_head_packed);

    next_free_[i].store(old_head.index, std::memory_order_relaxed);
    shard.free_head.store(PackTaggedIndex(TaggedIndex(i, 0U)), std::memory_order_relaxed);
    shard.available_count.fetch_add(1U, std::memory_order_relaxed);
  }
}

DMABufferPool::~DMABufferPool() {
  for (auto* buf : buffers_) {
#if STREAMING_DMA_ALIGNMENT > 0
    ::operator delete(buf, std::align_val_t{STREAMING_DMA_ALIGNMENT});
#else
    ::operator delete(buf);
#endif
  }
}

DataToken DMABufferPool::TryBorrowFromShard(uint32_t shard_idx) {
  BufferPoolShard& shard = shards_[shard_idx];

  uint64_t old_head_packed = shard.free_head.load(std::memory_order_acquire);

  while (true) {
    TaggedIndex old_head = UnpackTaggedIndex(old_head_packed);

    if (old_head.index == kInvalidIndex) {
      // This shard is empty
      return DataToken();
    }

    // Read next pointer BEFORE CAS
    uint32_t next = next_free_[old_head.index].load(std::memory_order_relaxed);

    // Create new head with incremented version (ABA protection)
    TaggedIndex new_head(next, old_head.version + 1U);
    uint64_t new_head_packed = PackTaggedIndex(new_head);

    // Try to CAS the head
    if (shard.free_head.compare_exchange_weak(old_head_packed, new_head_packed, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
      // Successfully borrowed buffer
      shard.available_count.fetch_sub(1U, std::memory_order_relaxed);
      borrow_count_.fetch_add(1U, std::memory_order_relaxed);

      auto now = std::chrono::steady_clock::now();
      uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

      // Zero heap allocation: function pointer + context instead of new DMABufferReleaser
      return DataToken(buffers_[old_head.index], buffer_size_, timestamp,
                       &DMABufferPool::ReleaseBuffer, this, old_head.index);
    }
    // CAS failed, retry with updated value
  }
}

DataToken DMABufferPool::Borrow() {
  // First, try the preferred shard for this thread
  uint32_t preferred_shard = GetShardIndex();
  DataToken token = TryBorrowFromShard(preferred_shard);

  if (token.Valid()) {
    return token;
  }

  // If preferred shard is empty, try other shards (work stealing)
  for (uint32_t i = 1U; i < shard_count_; ++i) {
    uint32_t shard_idx = (preferred_shard + i) % shard_count_;
    token = TryBorrowFromShard(shard_idx);
    if (token.Valid()) {
      return token;
    }
  }

  // All shards are empty
  return DataToken();
}

void DMABufferPool::Return(uint32_t index) {
  if (index >= buffer_count_) {
    return;  // Invalid index
  }

  // Return to the buffer's original shard
  uint32_t shard_idx = GetBufferShard(index);
  BufferPoolShard& shard = shards_[shard_idx];

  uint64_t old_head_packed = shard.free_head.load(std::memory_order_acquire);

  while (true) {
    TaggedIndex old_head = UnpackTaggedIndex(old_head_packed);

    // Set next pointer of returned buffer to current head
    next_free_[index].store(old_head.index, std::memory_order_relaxed);

    // Create new head with incremented version (ABA protection)
    TaggedIndex new_head(index, old_head.version + 1U);
    uint64_t new_head_packed = PackTaggedIndex(new_head);

    // Try to CAS the head
    if (shard.free_head.compare_exchange_weak(old_head_packed, new_head_packed, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
      // Successfully returned buffer
      shard.available_count.fetch_add(1U, std::memory_order_relaxed);
      return_count_.fetch_add(1U, std::memory_order_relaxed);
      return;
    }
    // CAS failed, retry with updated value
  }
}

uint32_t DMABufferPool::AvailableBuffers() const {
  uint32_t total = 0U;
  for (uint32_t s = 0U; s < shard_count_; ++s) {
    total += shards_[s].available_count.load(std::memory_order_relaxed);
  }
  return total;
}

} /* namespace streaming */
