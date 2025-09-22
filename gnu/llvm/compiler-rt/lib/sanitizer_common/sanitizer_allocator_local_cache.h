//===-- sanitizer_allocator_local_cache.h -----------------------*- C++ -*-===//
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

// Cache used by SizeClassAllocator64.
template <class SizeClassAllocator>
struct SizeClassAllocator64LocalCache {
  typedef SizeClassAllocator Allocator;
  typedef MemoryMapper<Allocator> MemoryMapperT;

  void Init(AllocatorGlobalStats *s) {
    stats_.Init();
    if (s)
      s->Register(&stats_);
  }

  void Destroy(SizeClassAllocator *allocator, AllocatorGlobalStats *s) {
    Drain(allocator);
    if (s)
      s->Unregister(&stats_);
  }

  void *Allocate(SizeClassAllocator *allocator, uptr class_id) {
    CHECK_NE(class_id, 0UL);
    CHECK_LT(class_id, kNumClasses);
    PerClass *c = &per_class_[class_id];
    if (UNLIKELY(c->count == 0)) {
      if (UNLIKELY(!Refill(c, allocator, class_id)))
        return nullptr;
      DCHECK_GT(c->count, 0);
    }
    CompactPtrT chunk = c->chunks[--c->count];
    stats_.Add(AllocatorStatAllocated, c->class_size);
    return reinterpret_cast<void *>(allocator->CompactPtrToPointer(
        allocator->GetRegionBeginBySizeClass(class_id), chunk));
  }

  void Deallocate(SizeClassAllocator *allocator, uptr class_id, void *p) {
    CHECK_NE(class_id, 0UL);
    CHECK_LT(class_id, kNumClasses);
    // If the first allocator call on a new thread is a deallocation, then
    // max_count will be zero, leading to check failure.
    PerClass *c = &per_class_[class_id];
    InitCache(c);
    if (UNLIKELY(c->count == c->max_count))
      DrainHalfMax(c, allocator, class_id);
    CompactPtrT chunk = allocator->PointerToCompactPtr(
        allocator->GetRegionBeginBySizeClass(class_id),
        reinterpret_cast<uptr>(p));
    c->chunks[c->count++] = chunk;
    stats_.Sub(AllocatorStatAllocated, c->class_size);
  }

  void Drain(SizeClassAllocator *allocator) {
    MemoryMapperT memory_mapper(*allocator);
    for (uptr i = 1; i < kNumClasses; i++) {
      PerClass *c = &per_class_[i];
      while (c->count > 0) Drain(&memory_mapper, c, allocator, i, c->count);
    }
  }

 private:
  typedef typename Allocator::SizeClassMapT SizeClassMap;
  static const uptr kNumClasses = SizeClassMap::kNumClasses;
  typedef typename Allocator::CompactPtrT CompactPtrT;

  struct PerClass {
    u32 count;
    u32 max_count;
    uptr class_size;
    CompactPtrT chunks[2 * SizeClassMap::kMaxNumCachedHint];
  };
  PerClass per_class_[kNumClasses];
  AllocatorStats stats_;

  void InitCache(PerClass *c) {
    if (LIKELY(c->max_count))
      return;
    for (uptr i = 1; i < kNumClasses; i++) {
      PerClass *c = &per_class_[i];
      const uptr size = Allocator::ClassIdToSize(i);
      c->max_count = 2 * SizeClassMap::MaxCachedHint(size);
      c->class_size = size;
    }
    DCHECK_NE(c->max_count, 0UL);
  }

  NOINLINE bool Refill(PerClass *c, SizeClassAllocator *allocator,
                       uptr class_id) {
    InitCache(c);
    const uptr num_requested_chunks = c->max_count / 2;
    if (UNLIKELY(!allocator->GetFromAllocator(&stats_, class_id, c->chunks,
                                              num_requested_chunks)))
      return false;
    c->count = num_requested_chunks;
    return true;
  }

  NOINLINE void DrainHalfMax(PerClass *c, SizeClassAllocator *allocator,
                             uptr class_id) {
    MemoryMapperT memory_mapper(*allocator);
    Drain(&memory_mapper, c, allocator, class_id, c->max_count / 2);
  }

  void Drain(MemoryMapperT *memory_mapper, PerClass *c,
             SizeClassAllocator *allocator, uptr class_id, uptr count) {
    CHECK_GE(c->count, count);
    const uptr first_idx_to_drain = c->count - count;
    c->count -= count;
    allocator->ReturnToAllocator(memory_mapper, &stats_, class_id,
                                 &c->chunks[first_idx_to_drain], count);
  }
};

// Cache used by SizeClassAllocator32.
template <class SizeClassAllocator>
struct SizeClassAllocator32LocalCache {
  typedef SizeClassAllocator Allocator;
  typedef typename Allocator::TransferBatch TransferBatch;

  void Init(AllocatorGlobalStats *s) {
    stats_.Init();
    if (s)
      s->Register(&stats_);
  }

  // Returns a TransferBatch suitable for class_id.
  TransferBatch *CreateBatch(uptr class_id, SizeClassAllocator *allocator,
                             TransferBatch *b) {
    if (uptr batch_class_id = per_class_[class_id].batch_class_id)
      return (TransferBatch*)Allocate(allocator, batch_class_id);
    return b;
  }

  // Destroys TransferBatch b.
  void DestroyBatch(uptr class_id, SizeClassAllocator *allocator,
                    TransferBatch *b) {
    if (uptr batch_class_id = per_class_[class_id].batch_class_id)
      Deallocate(allocator, batch_class_id, b);
  }

  void Destroy(SizeClassAllocator *allocator, AllocatorGlobalStats *s) {
    Drain(allocator);
    if (s)
      s->Unregister(&stats_);
  }

  void *Allocate(SizeClassAllocator *allocator, uptr class_id) {
    CHECK_NE(class_id, 0UL);
    CHECK_LT(class_id, kNumClasses);
    PerClass *c = &per_class_[class_id];
    if (UNLIKELY(c->count == 0)) {
      if (UNLIKELY(!Refill(c, allocator, class_id)))
        return nullptr;
      DCHECK_GT(c->count, 0);
    }
    void *res = c->batch[--c->count];
    PREFETCH(c->batch[c->count - 1]);
    stats_.Add(AllocatorStatAllocated, c->class_size);
    return res;
  }

  void Deallocate(SizeClassAllocator *allocator, uptr class_id, void *p) {
    CHECK_NE(class_id, 0UL);
    CHECK_LT(class_id, kNumClasses);
    // If the first allocator call on a new thread is a deallocation, then
    // max_count will be zero, leading to check failure.
    PerClass *c = &per_class_[class_id];
    InitCache(c);
    if (UNLIKELY(c->count == c->max_count))
      Drain(c, allocator, class_id);
    c->batch[c->count++] = p;
    stats_.Sub(AllocatorStatAllocated, c->class_size);
  }

  void Drain(SizeClassAllocator *allocator) {
    for (uptr i = 1; i < kNumClasses; i++) {
      PerClass *c = &per_class_[i];
      while (c->count > 0)
        Drain(c, allocator, i);
    }
  }

 private:
  typedef typename Allocator::SizeClassMapT SizeClassMap;
  static const uptr kBatchClassID = SizeClassMap::kBatchClassID;
  static const uptr kNumClasses = SizeClassMap::kNumClasses;
  // If kUseSeparateSizeClassForBatch is true, all TransferBatch objects are
  // allocated from kBatchClassID size class (except for those that are needed
  // for kBatchClassID itself). The goal is to have TransferBatches in a totally
  // different region of RAM to improve security.
  static const bool kUseSeparateSizeClassForBatch =
      Allocator::kUseSeparateSizeClassForBatch;

  struct PerClass {
    uptr count;
    uptr max_count;
    uptr class_size;
    uptr batch_class_id;
    void *batch[2 * TransferBatch::kMaxNumCached];
  };
  PerClass per_class_[kNumClasses];
  AllocatorStats stats_;

  void InitCache(PerClass *c) {
    if (LIKELY(c->max_count))
      return;
    const uptr batch_class_id = SizeClassMap::ClassID(sizeof(TransferBatch));
    for (uptr i = 1; i < kNumClasses; i++) {
      PerClass *c = &per_class_[i];
      const uptr size = Allocator::ClassIdToSize(i);
      const uptr max_cached = TransferBatch::MaxCached(size);
      c->max_count = 2 * max_cached;
      c->class_size = size;
      // Precompute the class id to use to store batches for the current class
      // id. 0 means the class size is large enough to store a batch within one
      // of the chunks. If using a separate size class, it will always be
      // kBatchClassID, except for kBatchClassID itself.
      if (kUseSeparateSizeClassForBatch) {
        c->batch_class_id = (i == kBatchClassID) ? 0 : kBatchClassID;
      } else {
        c->batch_class_id = (size <
          TransferBatch::AllocationSizeRequiredForNElements(max_cached)) ?
              batch_class_id : 0;
      }
    }
    DCHECK_NE(c->max_count, 0UL);
  }

  NOINLINE bool Refill(PerClass *c, SizeClassAllocator *allocator,
                       uptr class_id) {
    InitCache(c);
    TransferBatch *b = allocator->AllocateBatch(&stats_, this, class_id);
    if (UNLIKELY(!b))
      return false;
    CHECK_GT(b->Count(), 0);
    b->CopyToArray(c->batch);
    c->count = b->Count();
    DestroyBatch(class_id, allocator, b);
    return true;
  }

  NOINLINE void Drain(PerClass *c, SizeClassAllocator *allocator,
                      uptr class_id) {
    const uptr count = Min(c->max_count / 2, c->count);
    const uptr first_idx_to_drain = c->count - count;
    TransferBatch *b = CreateBatch(
        class_id, allocator, (TransferBatch *)c->batch[first_idx_to_drain]);
    // Failure to allocate a batch while releasing memory is non recoverable.
    // TODO(alekseys): Figure out how to do it without allocating a new batch.
    if (UNLIKELY(!b)) {
      Report("FATAL: Internal error: %s's allocator failed to allocate a "
             "transfer batch.\n", SanitizerToolName);
      Die();
    }
    b->SetFromArray(&c->batch[first_idx_to_drain], count);
    c->count -= count;
    allocator->DeallocateBatch(&stats_, class_id, b);
  }
};
