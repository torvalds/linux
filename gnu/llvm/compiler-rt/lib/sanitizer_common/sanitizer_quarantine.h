//===-- sanitizer_quarantine.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Memory quarantine for AddressSanitizer and potentially other tools.
// Quarantine caches some specified amount of memory in per-thread caches,
// then evicts to global FIFO queue. When the queue reaches specified threshold,
// oldest memory is recycled.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_QUARANTINE_H
#define SANITIZER_QUARANTINE_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_mutex.h"
#include "sanitizer_list.h"

namespace __sanitizer {

template<typename Node> class QuarantineCache;

struct QuarantineBatch {
  static const uptr kSize = 1021;
  QuarantineBatch *next;
  uptr size;
  uptr count;
  void *batch[kSize];

  void init(void *ptr, uptr size) {
    count = 1;
    batch[0] = ptr;
    this->size = size + sizeof(QuarantineBatch);  // Account for the batch size.
  }

  // The total size of quarantined nodes recorded in this batch.
  uptr quarantined_size() const {
    return size - sizeof(QuarantineBatch);
  }

  void push_back(void *ptr, uptr size) {
    CHECK_LT(count, kSize);
    batch[count++] = ptr;
    this->size += size;
  }

  bool can_merge(const QuarantineBatch* const from) const {
    return count + from->count <= kSize;
  }

  void merge(QuarantineBatch* const from) {
    CHECK_LE(count + from->count, kSize);
    CHECK_GE(size, sizeof(QuarantineBatch));

    for (uptr i = 0; i < from->count; ++i)
      batch[count + i] = from->batch[i];
    count += from->count;
    size += from->quarantined_size();

    from->count = 0;
    from->size = sizeof(QuarantineBatch);
  }
};

COMPILER_CHECK(sizeof(QuarantineBatch) <= (1 << 13));  // 8Kb.

template<typename Callback, typename Node>
class Quarantine {
 public:
  typedef QuarantineCache<Callback> Cache;

  explicit Quarantine(LinkerInitialized)
      : cache_(LINKER_INITIALIZED) {
  }

  void Init(uptr size, uptr cache_size) {
    // Thread local quarantine size can be zero only when global quarantine size
    // is zero (it allows us to perform just one atomic read per Put() call).
    CHECK((size == 0 && cache_size == 0) || cache_size != 0);

    atomic_store_relaxed(&max_size_, size);
    atomic_store_relaxed(&min_size_, size / 10 * 9);  // 90% of max size.
    atomic_store_relaxed(&max_cache_size_, cache_size);

    cache_mutex_.Init();
    recycle_mutex_.Init();
  }

  uptr GetMaxSize() const { return atomic_load_relaxed(&max_size_); }
  uptr GetMaxCacheSize() const { return atomic_load_relaxed(&max_cache_size_); }

  void Put(Cache *c, Callback cb, Node *ptr, uptr size) {
    uptr max_cache_size = GetMaxCacheSize();
    if (max_cache_size && size <= GetMaxSize()) {
      cb.PreQuarantine(ptr);
      c->Enqueue(cb, ptr, size);
    } else {
      // GetMaxCacheSize() == 0 only when GetMaxSize() == 0 (see Init).
      cb.RecyclePassThrough(ptr);
    }
    // Check cache size anyway to accommodate for runtime cache_size change.
    if (c->Size() > max_cache_size)
      Drain(c, cb);
  }

  void NOINLINE Drain(Cache *c, Callback cb) {
    {
      SpinMutexLock l(&cache_mutex_);
      cache_.Transfer(c);
    }
    if (cache_.Size() > GetMaxSize() && recycle_mutex_.TryLock())
      Recycle(atomic_load_relaxed(&min_size_), cb);
  }

  void NOINLINE DrainAndRecycle(Cache *c, Callback cb) {
    {
      SpinMutexLock l(&cache_mutex_);
      cache_.Transfer(c);
    }
    recycle_mutex_.Lock();
    Recycle(0, cb);
  }

  void PrintStats() const {
    // It assumes that the world is stopped, just as the allocator's PrintStats.
    Printf("Quarantine limits: global: %zdMb; thread local: %zdKb\n",
           GetMaxSize() >> 20, GetMaxCacheSize() >> 10);
    cache_.PrintStats();
  }

 private:
  // Read-only data.
  char pad0_[kCacheLineSize];
  atomic_uintptr_t max_size_;
  atomic_uintptr_t min_size_;
  atomic_uintptr_t max_cache_size_;
  char pad1_[kCacheLineSize];
  StaticSpinMutex cache_mutex_;
  StaticSpinMutex recycle_mutex_;
  Cache cache_;
  char pad2_[kCacheLineSize];

  void NOINLINE Recycle(uptr min_size, Callback cb)
      SANITIZER_REQUIRES(recycle_mutex_) SANITIZER_RELEASE(recycle_mutex_) {
    Cache tmp;
    {
      SpinMutexLock l(&cache_mutex_);
      // Go over the batches and merge partially filled ones to
      // save some memory, otherwise batches themselves (since the memory used
      // by them is counted against quarantine limit) can overcome the actual
      // user's quarantined chunks, which diminishes the purpose of the
      // quarantine.
      uptr cache_size = cache_.Size();
      uptr overhead_size = cache_.OverheadSize();
      CHECK_GE(cache_size, overhead_size);
      // Do the merge only when overhead exceeds this predefined limit (might
      // require some tuning). It saves us merge attempt when the batch list
      // quarantine is unlikely to contain batches suitable for merge.
      const uptr kOverheadThresholdPercents = 100;
      if (cache_size > overhead_size &&
          overhead_size * (100 + kOverheadThresholdPercents) >
              cache_size * kOverheadThresholdPercents) {
        cache_.MergeBatches(&tmp);
      }
      // Extract enough chunks from the quarantine to get below the max
      // quarantine size and leave some leeway for the newly quarantined chunks.
      while (cache_.Size() > min_size) {
        tmp.EnqueueBatch(cache_.DequeueBatch());
      }
    }
    recycle_mutex_.Unlock();
    DoRecycle(&tmp, cb);
  }

  void NOINLINE DoRecycle(Cache *c, Callback cb) {
    while (QuarantineBatch *b = c->DequeueBatch()) {
      const uptr kPrefetch = 16;
      CHECK(kPrefetch <= ARRAY_SIZE(b->batch));
      for (uptr i = 0; i < kPrefetch; i++)
        PREFETCH(b->batch[i]);
      for (uptr i = 0, count = b->count; i < count; i++) {
        if (i + kPrefetch < count)
          PREFETCH(b->batch[i + kPrefetch]);
        cb.Recycle((Node*)b->batch[i]);
      }
      cb.Deallocate(b);
    }
  }
};

// Per-thread cache of memory blocks.
template<typename Callback>
class QuarantineCache {
 public:
  explicit QuarantineCache(LinkerInitialized) {
  }

  QuarantineCache()
      : size_() {
    list_.clear();
  }

  // Total memory used, including internal accounting.
  uptr Size() const {
    return atomic_load_relaxed(&size_);
  }

  // Memory used for internal accounting.
  uptr OverheadSize() const {
    return list_.size() * sizeof(QuarantineBatch);
  }

  void Enqueue(Callback cb, void *ptr, uptr size) {
    if (list_.empty() || list_.back()->count == QuarantineBatch::kSize) {
      QuarantineBatch *b = (QuarantineBatch *)cb.Allocate(sizeof(*b));
      CHECK(b);
      b->init(ptr, size);
      EnqueueBatch(b);
    } else {
      list_.back()->push_back(ptr, size);
      SizeAdd(size);
    }
  }

  void Transfer(QuarantineCache *from_cache) {
    list_.append_back(&from_cache->list_);
    SizeAdd(from_cache->Size());

    atomic_store_relaxed(&from_cache->size_, 0);
  }

  void EnqueueBatch(QuarantineBatch *b) {
    list_.push_back(b);
    SizeAdd(b->size);
  }

  QuarantineBatch *DequeueBatch() {
    if (list_.empty())
      return nullptr;
    QuarantineBatch *b = list_.front();
    list_.pop_front();
    SizeSub(b->size);
    return b;
  }

  void MergeBatches(QuarantineCache *to_deallocate) {
    uptr extracted_size = 0;
    QuarantineBatch *current = list_.front();
    while (current && current->next) {
      if (current->can_merge(current->next)) {
        QuarantineBatch *extracted = current->next;
        // Move all the chunks into the current batch.
        current->merge(extracted);
        CHECK_EQ(extracted->count, 0);
        CHECK_EQ(extracted->size, sizeof(QuarantineBatch));
        // Remove the next batch from the list and account for its size.
        list_.extract(current, extracted);
        extracted_size += extracted->size;
        // Add it to deallocation list.
        to_deallocate->EnqueueBatch(extracted);
      } else {
        current = current->next;
      }
    }
    SizeSub(extracted_size);
  }

  void PrintStats() const {
    uptr batch_count = 0;
    uptr total_overhead_bytes = 0;
    uptr total_bytes = 0;
    uptr total_quarantine_chunks = 0;
    for (List::ConstIterator it = list_.begin(); it != list_.end(); ++it) {
      batch_count++;
      total_bytes += (*it).size;
      total_overhead_bytes += (*it).size - (*it).quarantined_size();
      total_quarantine_chunks += (*it).count;
    }
    uptr quarantine_chunks_capacity = batch_count * QuarantineBatch::kSize;
    int chunks_usage_percent = quarantine_chunks_capacity == 0 ?
        0 : total_quarantine_chunks * 100 / quarantine_chunks_capacity;
    uptr total_quarantined_bytes = total_bytes - total_overhead_bytes;
    int memory_overhead_percent = total_quarantined_bytes == 0 ?
        0 : total_overhead_bytes * 100 / total_quarantined_bytes;
    Printf("Global quarantine stats: batches: %zd; bytes: %zd (user: %zd); "
           "chunks: %zd (capacity: %zd); %d%% chunks used; %d%% memory overhead"
           "\n",
           batch_count, total_bytes, total_quarantined_bytes,
           total_quarantine_chunks, quarantine_chunks_capacity,
           chunks_usage_percent, memory_overhead_percent);
  }

 private:
  typedef IntrusiveList<QuarantineBatch> List;

  List list_;
  atomic_uintptr_t size_;

  void SizeAdd(uptr add) {
    atomic_store_relaxed(&size_, Size() + add);
  }
  void SizeSub(uptr sub) {
    atomic_store_relaxed(&size_, Size() - sub);
  }
};

} // namespace __sanitizer

#endif // SANITIZER_QUARANTINE_H
