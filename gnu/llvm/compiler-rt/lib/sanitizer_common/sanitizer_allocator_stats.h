//===-- sanitizer_allocator_stats.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Part of the Sanitizer Allocator.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ALLOCATOR_H
#error This file must be included inside sanitizer_allocator.h
#endif

// Memory allocator statistics
enum AllocatorStat {
  AllocatorStatAllocated,
  AllocatorStatMapped,
  AllocatorStatCount
};

typedef uptr AllocatorStatCounters[AllocatorStatCount];

// Per-thread stats, live in per-thread cache.
class AllocatorStats {
 public:
  void Init() { internal_memset(this, 0, sizeof(*this)); }
  void Add(AllocatorStat i, uptr v) {
    atomic_fetch_add(&stats_[i], v, memory_order_relaxed);
  }

  void Sub(AllocatorStat i, uptr v) {
    atomic_fetch_sub(&stats_[i], v, memory_order_relaxed);
  }

  void Set(AllocatorStat i, uptr v) {
    atomic_store(&stats_[i], v, memory_order_relaxed);
  }

  uptr Get(AllocatorStat i) const {
    return atomic_load(&stats_[i], memory_order_relaxed);
  }

 private:
  friend class AllocatorGlobalStats;
  AllocatorStats *next_;
  AllocatorStats *prev_;
  atomic_uintptr_t stats_[AllocatorStatCount];
};

// Global stats, used for aggregation and querying.
class AllocatorGlobalStats : public AllocatorStats {
 public:
  void Init() {
    internal_memset(this, 0, sizeof(*this));
  }

  void Register(AllocatorStats *s) {
    SpinMutexLock l(&mu_);
    LazyInit();
    s->next_ = next_;
    s->prev_ = this;
    next_->prev_ = s;
    next_ = s;
  }

  void Unregister(AllocatorStats *s) {
    SpinMutexLock l(&mu_);
    s->prev_->next_ = s->next_;
    s->next_->prev_ = s->prev_;
    for (int i = 0; i < AllocatorStatCount; i++)
      Add(AllocatorStat(i), s->Get(AllocatorStat(i)));
  }

  void Get(AllocatorStatCounters s) const {
    internal_memset(s, 0, AllocatorStatCount * sizeof(uptr));
    SpinMutexLock l(&mu_);
    const AllocatorStats *stats = this;
    for (; stats;) {
      for (int i = 0; i < AllocatorStatCount; i++)
        s[i] += stats->Get(AllocatorStat(i));
      stats = stats->next_;
      if (stats == this)
        break;
    }
    // All stats must be non-negative.
    for (int i = 0; i < AllocatorStatCount; i++)
      s[i] = ((sptr)s[i]) >= 0 ? s[i] : 0;
  }

 private:
  void LazyInit() {
    if (!next_) {
      next_ = this;
      prev_ = this;
    }
  }

  mutable StaticSpinMutex mu_;
};


