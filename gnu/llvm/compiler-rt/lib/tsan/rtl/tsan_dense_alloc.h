//===-- tsan_dense_alloc.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// A DenseSlabAlloc is a freelist-based allocator of fixed-size objects.
// DenseSlabAllocCache is a thread-local cache for DenseSlabAlloc.
// The only difference with traditional slab allocators is that DenseSlabAlloc
// allocates/free indices of objects and provide a functionality to map
// the index onto the real pointer. The index is u32, that is, 2 times smaller
// than uptr (hense the Dense prefix).
//===----------------------------------------------------------------------===//
#ifndef TSAN_DENSE_ALLOC_H
#define TSAN_DENSE_ALLOC_H

#include "sanitizer_common/sanitizer_common.h"
#include "tsan_defs.h"

namespace __tsan {

class DenseSlabAllocCache {
  static const uptr kSize = 128;
  typedef u32 IndexT;
  uptr pos;
  IndexT cache[kSize];
  template <typename, uptr, uptr, u64>
  friend class DenseSlabAlloc;
};

template <typename T, uptr kL1Size, uptr kL2Size, u64 kReserved = 0>
class DenseSlabAlloc {
 public:
  typedef DenseSlabAllocCache Cache;
  typedef typename Cache::IndexT IndexT;

  static_assert((kL1Size & (kL1Size - 1)) == 0,
                "kL1Size must be a power-of-two");
  static_assert((kL2Size & (kL2Size - 1)) == 0,
                "kL2Size must be a power-of-two");
  static_assert((kL1Size * kL2Size) <= (1ull << (sizeof(IndexT) * 8)),
                "kL1Size/kL2Size are too large");
  static_assert(((kL1Size * kL2Size - 1) & kReserved) == 0,
                "reserved bits don't fit");
  static_assert(sizeof(T) > sizeof(IndexT),
                "it doesn't make sense to use dense alloc");

  DenseSlabAlloc(LinkerInitialized, const char *name) : name_(name) {}

  explicit DenseSlabAlloc(const char *name)
      : DenseSlabAlloc(LINKER_INITIALIZED, name) {
    // It can be very large.
    // Don't page it in for linker initialized objects.
    internal_memset(map_, 0, sizeof(map_));
  }

  ~DenseSlabAlloc() {
    for (uptr i = 0; i < kL1Size; i++) {
      if (map_[i] != 0)
        UnmapOrDie(map_[i], kL2Size * sizeof(T));
    }
  }

  IndexT Alloc(Cache *c) {
    if (c->pos == 0)
      Refill(c);
    return c->cache[--c->pos];
  }

  void Free(Cache *c, IndexT idx) {
    DCHECK_NE(idx, 0);
    if (c->pos == Cache::kSize)
      Drain(c);
    c->cache[c->pos++] = idx;
  }

  T *Map(IndexT idx) {
    DCHECK_NE(idx, 0);
    DCHECK_LE(idx, kL1Size * kL2Size);
    return &map_[idx / kL2Size][idx % kL2Size];
  }

  void FlushCache(Cache *c) {
    while (c->pos) Drain(c);
  }

  void InitCache(Cache *c) {
    c->pos = 0;
    internal_memset(c->cache, 0, sizeof(c->cache));
  }

  uptr AllocatedMemory() const {
    return atomic_load_relaxed(&fillpos_) * kL2Size * sizeof(T);
  }

  template <typename Func>
  void ForEach(Func func) {
    Lock lock(&mtx_);
    uptr fillpos = atomic_load_relaxed(&fillpos_);
    for (uptr l1 = 0; l1 < fillpos; l1++) {
      for (IndexT l2 = l1 == 0 ? 1 : 0; l2 < kL2Size; l2++) func(&map_[l1][l2]);
    }
  }

 private:
  T *map_[kL1Size];
  Mutex mtx_;
  // The freelist is organized as a lock-free stack of batches of nodes.
  // The stack itself uses Block::next links, while the batch within each
  // stack node uses Block::batch links.
  // Low 32-bits of freelist_ is the node index, top 32-bits is ABA-counter.
  atomic_uint64_t freelist_ = {0};
  atomic_uintptr_t fillpos_ = {0};
  const char *const name_;

  struct Block {
    IndexT next;
    IndexT batch;
  };

  Block *MapBlock(IndexT idx) { return reinterpret_cast<Block *>(Map(idx)); }

  static constexpr u64 kCounterInc = 1ull << 32;
  static constexpr u64 kCounterMask = ~(kCounterInc - 1);

  NOINLINE void Refill(Cache *c) {
    // Pop 1 batch of nodes from the freelist.
    IndexT idx;
    u64 xchg;
    u64 cmp = atomic_load(&freelist_, memory_order_acquire);
    do {
      idx = static_cast<IndexT>(cmp);
      if (!idx)
        return AllocSuperBlock(c);
      Block *ptr = MapBlock(idx);
      xchg = ptr->next | (cmp & kCounterMask);
    } while (!atomic_compare_exchange_weak(&freelist_, &cmp, xchg,
                                           memory_order_acq_rel));
    // Unpack it into c->cache.
    while (idx) {
      c->cache[c->pos++] = idx;
      idx = MapBlock(idx)->batch;
    }
  }

  NOINLINE void Drain(Cache *c) {
    // Build a batch of at most Cache::kSize / 2 nodes linked by Block::batch.
    IndexT head_idx = 0;
    for (uptr i = 0; i < Cache::kSize / 2 && c->pos; i++) {
      IndexT idx = c->cache[--c->pos];
      Block *ptr = MapBlock(idx);
      ptr->batch = head_idx;
      head_idx = idx;
    }
    // Push it onto the freelist stack.
    Block *head = MapBlock(head_idx);
    u64 xchg;
    u64 cmp = atomic_load(&freelist_, memory_order_acquire);
    do {
      head->next = static_cast<IndexT>(cmp);
      xchg = head_idx | (cmp & kCounterMask) + kCounterInc;
    } while (!atomic_compare_exchange_weak(&freelist_, &cmp, xchg,
                                           memory_order_acq_rel));
  }

  NOINLINE void AllocSuperBlock(Cache *c) {
    Lock lock(&mtx_);
    uptr fillpos = atomic_load_relaxed(&fillpos_);
    if (fillpos == kL1Size) {
      Printf("ThreadSanitizer: %s overflow (%zu*%zu). Dying.\n", name_, kL1Size,
             kL2Size);
      Die();
    }
    VPrintf(2, "ThreadSanitizer: growing %s: %zu out of %zu*%zu\n", name_,
            fillpos, kL1Size, kL2Size);
    T *batch = (T *)MmapOrDie(kL2Size * sizeof(T), name_);
    map_[fillpos] = batch;
    // Reserve 0 as invalid index.
    for (IndexT i = fillpos ? 0 : 1; i < kL2Size; i++) {
      new (batch + i) T;
      c->cache[c->pos++] = i + fillpos * kL2Size;
      if (c->pos == Cache::kSize)
        Drain(c);
    }
    atomic_store_relaxed(&fillpos_, fillpos + 1);
    CHECK(c->pos);
  }
};

}  // namespace __tsan

#endif  // TSAN_DENSE_ALLOC_H
