//===-- sanitizer_allocator_secondary.h -------------------------*- C++ -*-===//
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

// Fixed array to store LargeMmapAllocator chunks list, limited to 32K total
// allocated chunks. To be used in memory constrained or not memory hungry cases
// (currently, 32 bits and internal allocator).
class LargeMmapAllocatorPtrArrayStatic {
 public:
  inline void *Init() { return &p_[0]; }
  inline void EnsureSpace(uptr n) { CHECK_LT(n, kMaxNumChunks); }
 private:
  static const int kMaxNumChunks = 1 << 15;
  uptr p_[kMaxNumChunks];
};

// Much less restricted LargeMmapAllocator chunks list (comparing to
// PtrArrayStatic). Backed by mmaped memory region and can hold up to 1M chunks.
// ReservedAddressRange was used instead of just MAP_NORESERVE to achieve the
// same functionality in Fuchsia case, which does not support MAP_NORESERVE.
class LargeMmapAllocatorPtrArrayDynamic {
 public:
  inline void *Init() {
    uptr p = address_range_.Init(kMaxNumChunks * sizeof(uptr),
                                 SecondaryAllocatorName);
    CHECK(p);
    return reinterpret_cast<void*>(p);
  }

  inline void EnsureSpace(uptr n) {
    CHECK_LT(n, kMaxNumChunks);
    DCHECK(n <= n_reserved_);
    if (UNLIKELY(n == n_reserved_)) {
      address_range_.MapOrDie(
          reinterpret_cast<uptr>(address_range_.base()) +
              n_reserved_ * sizeof(uptr),
          kChunksBlockCount * sizeof(uptr));
      n_reserved_ += kChunksBlockCount;
    }
  }

 private:
  static const int kMaxNumChunks = 1 << 20;
  static const int kChunksBlockCount = 1 << 14;
  ReservedAddressRange address_range_;
  uptr n_reserved_;
};

#if SANITIZER_WORDSIZE == 32
typedef LargeMmapAllocatorPtrArrayStatic DefaultLargeMmapAllocatorPtrArray;
#else
typedef LargeMmapAllocatorPtrArrayDynamic DefaultLargeMmapAllocatorPtrArray;
#endif

// This class can (de)allocate only large chunks of memory using mmap/unmap.
// The main purpose of this allocator is to cover large and rare allocation
// sizes not covered by more efficient allocators (e.g. SizeClassAllocator64).
template <class MapUnmapCallback = NoOpMapUnmapCallback,
          class PtrArrayT = DefaultLargeMmapAllocatorPtrArray,
          class AddressSpaceViewTy = LocalAddressSpaceView>
class LargeMmapAllocator {
 public:
  using AddressSpaceView = AddressSpaceViewTy;
  void InitLinkerInitialized() {
    page_size_ = GetPageSizeCached();
    chunks_ = reinterpret_cast<Header**>(ptr_array_.Init());
  }

  void Init() {
    internal_memset(this, 0, sizeof(*this));
    InitLinkerInitialized();
  }

  void *Allocate(AllocatorStats *stat, const uptr size, uptr alignment) {
    CHECK(IsPowerOfTwo(alignment));
    uptr map_size = RoundUpMapSize(size);
    if (alignment > page_size_)
      map_size += alignment;
    // Overflow.
    if (map_size < size) {
      Report("WARNING: %s: LargeMmapAllocator allocation overflow: "
             "0x%zx bytes with 0x%zx alignment requested\n",
             SanitizerToolName, map_size, alignment);
      return nullptr;
    }
    uptr map_beg = reinterpret_cast<uptr>(
        MmapOrDieOnFatalError(map_size, SecondaryAllocatorName));
    if (!map_beg)
      return nullptr;
    CHECK(IsAligned(map_beg, page_size_));
    uptr map_end = map_beg + map_size;
    uptr res = map_beg + page_size_;
    if (res & (alignment - 1))  // Align.
      res += alignment - (res & (alignment - 1));
    MapUnmapCallback().OnMapSecondary(map_beg, map_size, res, size);
    CHECK(IsAligned(res, alignment));
    CHECK(IsAligned(res, page_size_));
    CHECK_GE(res + size, map_beg);
    CHECK_LE(res + size, map_end);
    Header *h = GetHeader(res);
    h->size = size;
    h->map_beg = map_beg;
    h->map_size = map_size;
    uptr size_log = MostSignificantSetBitIndex(map_size);
    CHECK_LT(size_log, ARRAY_SIZE(stats.by_size_log));
    {
      SpinMutexLock l(&mutex_);
      ptr_array_.EnsureSpace(n_chunks_);
      uptr idx = n_chunks_++;
      h->chunk_idx = idx;
      chunks_[idx] = h;
      chunks_sorted_ = false;
      stats.n_allocs++;
      stats.currently_allocated += map_size;
      stats.max_allocated = Max(stats.max_allocated, stats.currently_allocated);
      stats.by_size_log[size_log]++;
      stat->Add(AllocatorStatAllocated, map_size);
      stat->Add(AllocatorStatMapped, map_size);
    }
    return reinterpret_cast<void*>(res);
  }

  void Deallocate(AllocatorStats *stat, void *p) {
    Header *h = GetHeader(p);
    {
      SpinMutexLock l(&mutex_);
      uptr idx = h->chunk_idx;
      CHECK_EQ(chunks_[idx], h);
      CHECK_LT(idx, n_chunks_);
      chunks_[idx] = chunks_[--n_chunks_];
      chunks_[idx]->chunk_idx = idx;
      chunks_sorted_ = false;
      stats.n_frees++;
      stats.currently_allocated -= h->map_size;
      stat->Sub(AllocatorStatAllocated, h->map_size);
      stat->Sub(AllocatorStatMapped, h->map_size);
    }
    MapUnmapCallback().OnUnmap(h->map_beg, h->map_size);
    UnmapOrDie(reinterpret_cast<void*>(h->map_beg), h->map_size);
  }

  uptr TotalMemoryUsed() {
    SpinMutexLock l(&mutex_);
    uptr res = 0;
    for (uptr i = 0; i < n_chunks_; i++) {
      Header *h = chunks_[i];
      CHECK_EQ(h->chunk_idx, i);
      res += RoundUpMapSize(h->size);
    }
    return res;
  }

  bool PointerIsMine(const void *p) const {
    return GetBlockBegin(p) != nullptr;
  }

  uptr GetActuallyAllocatedSize(void *p) {
    return RoundUpTo(GetHeader(p)->size, page_size_);
  }

  // At least page_size_/2 metadata bytes is available.
  void *GetMetaData(const void *p) {
    // Too slow: CHECK_EQ(p, GetBlockBegin(p));
    if (!IsAligned(reinterpret_cast<uptr>(p), page_size_)) {
      Printf("%s: bad pointer %p\n", SanitizerToolName, p);
      CHECK(IsAligned(reinterpret_cast<uptr>(p), page_size_));
    }
    return GetHeader(p) + 1;
  }

  void *GetBlockBegin(const void *ptr) const {
    uptr p = reinterpret_cast<uptr>(ptr);
    SpinMutexLock l(&mutex_);
    uptr nearest_chunk = 0;
    Header *const *chunks = AddressSpaceView::Load(chunks_, n_chunks_);
    // Cache-friendly linear search.
    for (uptr i = 0; i < n_chunks_; i++) {
      uptr ch = reinterpret_cast<uptr>(chunks[i]);
      if (p < ch) continue;  // p is at left to this chunk, skip it.
      if (p - ch < p - nearest_chunk)
        nearest_chunk = ch;
    }
    if (!nearest_chunk)
      return nullptr;
    const Header *h =
        AddressSpaceView::Load(reinterpret_cast<Header *>(nearest_chunk));
    Header *h_ptr = reinterpret_cast<Header *>(nearest_chunk);
    CHECK_GE(nearest_chunk, h->map_beg);
    CHECK_LT(nearest_chunk, h->map_beg + h->map_size);
    CHECK_LE(nearest_chunk, p);
    if (h->map_beg + h->map_size <= p)
      return nullptr;
    return GetUser(h_ptr);
  }

  void EnsureSortedChunks() {
    if (chunks_sorted_) return;
    Header **chunks = AddressSpaceView::LoadWritable(chunks_, n_chunks_);
    Sort(reinterpret_cast<uptr *>(chunks), n_chunks_);
    for (uptr i = 0; i < n_chunks_; i++)
      AddressSpaceView::LoadWritable(chunks[i])->chunk_idx = i;
    chunks_sorted_ = true;
  }

  // This function does the same as GetBlockBegin, but is much faster.
  // Must be called with the allocator locked.
  void *GetBlockBeginFastLocked(const void *ptr) {
    mutex_.CheckLocked();
    uptr p = reinterpret_cast<uptr>(ptr);
    uptr n = n_chunks_;
    if (!n) return nullptr;
    EnsureSortedChunks();
    Header *const *chunks = AddressSpaceView::Load(chunks_, n_chunks_);
    auto min_mmap_ = reinterpret_cast<uptr>(chunks[0]);
    auto max_mmap_ = reinterpret_cast<uptr>(chunks[n - 1]) +
                     AddressSpaceView::Load(chunks[n - 1])->map_size;
    if (p < min_mmap_ || p >= max_mmap_)
      return nullptr;
    uptr beg = 0, end = n - 1;
    // This loop is a log(n) lower_bound. It does not check for the exact match
    // to avoid expensive cache-thrashing loads.
    while (end - beg >= 2) {
      uptr mid = (beg + end) / 2;  // Invariant: mid >= beg + 1
      if (p < reinterpret_cast<uptr>(chunks[mid]))
        end = mid - 1;  // We are not interested in chunks[mid].
      else
        beg = mid;  // chunks[mid] may still be what we want.
    }

    if (beg < end) {
      CHECK_EQ(beg + 1, end);
      // There are 2 chunks left, choose one.
      if (p >= reinterpret_cast<uptr>(chunks[end]))
        beg = end;
    }

    const Header *h = AddressSpaceView::Load(chunks[beg]);
    Header *h_ptr = chunks[beg];
    if (h->map_beg + h->map_size <= p || p < h->map_beg)
      return nullptr;
    return GetUser(h_ptr);
  }

  void PrintStats() {
    Printf("Stats: LargeMmapAllocator: allocated %zd times, "
           "remains %zd (%zd K) max %zd M; by size logs: ",
           stats.n_allocs, stats.n_allocs - stats.n_frees,
           stats.currently_allocated >> 10, stats.max_allocated >> 20);
    for (uptr i = 0; i < ARRAY_SIZE(stats.by_size_log); i++) {
      uptr c = stats.by_size_log[i];
      if (!c) continue;
      Printf("%zd:%zd; ", i, c);
    }
    Printf("\n");
  }

  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() SANITIZER_ACQUIRE(mutex_) { mutex_.Lock(); }

  void ForceUnlock() SANITIZER_RELEASE(mutex_) { mutex_.Unlock(); }

  // Iterate over all existing chunks.
  // The allocator must be locked when calling this function.
  void ForEachChunk(ForEachChunkCallback callback, void *arg) {
    EnsureSortedChunks();  // Avoid doing the sort while iterating.
    const Header *const *chunks = AddressSpaceView::Load(chunks_, n_chunks_);
    for (uptr i = 0; i < n_chunks_; i++) {
      const Header *t = chunks[i];
      callback(reinterpret_cast<uptr>(GetUser(t)), arg);
      // Consistency check: verify that the array did not change.
      CHECK_EQ(chunks[i], t);
      CHECK_EQ(AddressSpaceView::Load(chunks[i])->chunk_idx, i);
    }
  }

 private:
  struct Header {
    uptr map_beg;
    uptr map_size;
    uptr size;
    uptr chunk_idx;
  };

  Header *GetHeader(uptr p) {
    CHECK(IsAligned(p, page_size_));
    return reinterpret_cast<Header*>(p - page_size_);
  }
  Header *GetHeader(const void *p) {
    return GetHeader(reinterpret_cast<uptr>(p));
  }

  void *GetUser(const Header *h) const {
    CHECK(IsAligned((uptr)h, page_size_));
    return reinterpret_cast<void*>(reinterpret_cast<uptr>(h) + page_size_);
  }

  uptr RoundUpMapSize(uptr size) {
    return RoundUpTo(size, page_size_) + page_size_;
  }

  uptr page_size_;
  Header **chunks_;
  PtrArrayT ptr_array_;
  uptr n_chunks_;
  bool chunks_sorted_;
  struct Stats {
    uptr n_allocs, n_frees, currently_allocated, max_allocated, by_size_log[64];
  } stats;
  mutable StaticSpinMutex mutex_;
};
