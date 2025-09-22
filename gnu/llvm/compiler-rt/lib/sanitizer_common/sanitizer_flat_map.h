//===-- sanitizer_flat_map.h ------------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_FLAT_MAP_H
#define SANITIZER_FLAT_MAP_H

#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_local_address_space_view.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

// Maps integers in rage [0, kSize) to values.
template <typename T, u64 kSize,
          typename AddressSpaceViewTy = LocalAddressSpaceView>
class FlatMap {
 public:
  using AddressSpaceView = AddressSpaceViewTy;
  void Init() { internal_memset(map_, 0, sizeof(map_)); }

  constexpr uptr size() const { return kSize; }

  bool contains(uptr idx) const {
    CHECK_LT(idx, kSize);
    return true;
  }

  T &operator[](uptr idx) {
    DCHECK_LT(idx, kSize);
    return map_[idx];
  }

  const T &operator[](uptr idx) const {
    DCHECK_LT(idx, kSize);
    return map_[idx];
  }

 private:
  T map_[kSize];
};

// TwoLevelMap maps integers in range [0, kSize1*kSize2) to values.
// It is implemented as a two-dimensional array: array of kSize1 pointers
// to kSize2-byte arrays. The secondary arrays are mmaped on demand.
// Each value is initially zero and can be set to something else only once.
// Setting and getting values from multiple threads is safe w/o extra locking.
template <typename T, u64 kSize1, u64 kSize2,
          typename AddressSpaceViewTy = LocalAddressSpaceView>
class TwoLevelMap {
  static_assert(IsPowerOfTwo(kSize2), "Use a power of two for performance.");

 public:
  using AddressSpaceView = AddressSpaceViewTy;
  void Init() {
    mu_.Init();
    internal_memset(map1_, 0, sizeof(map1_));
  }

  void TestOnlyUnmap() {
    for (uptr i = 0; i < kSize1; i++) {
      T *p = Get(i);
      if (!p)
        continue;
      UnmapOrDie(p, kSize2);
    }
    Init();
  }

  uptr MemoryUsage() const {
    uptr res = 0;
    for (uptr i = 0; i < kSize1; i++) {
      T *p = Get(i);
      if (!p)
        continue;
      res += MmapSize();
    }
    return res;
  }

  constexpr uptr size() const { return kSize1 * kSize2; }
  constexpr uptr size1() const { return kSize1; }
  constexpr uptr size2() const { return kSize2; }

  bool contains(uptr idx) const {
    CHECK_LT(idx, kSize1 * kSize2);
    return Get(idx / kSize2);
  }

  const T &operator[](uptr idx) const {
    DCHECK_LT(idx, kSize1 * kSize2);
    T *map2 = GetOrCreate(idx / kSize2);
    return *AddressSpaceView::Load(&map2[idx % kSize2]);
  }

  T &operator[](uptr idx) {
    DCHECK_LT(idx, kSize1 * kSize2);
    T *map2 = GetOrCreate(idx / kSize2);
    return *AddressSpaceView::LoadWritable(&map2[idx % kSize2]);
  }

  void Lock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS { mu_.Lock(); }

  void Unlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS { mu_.Unlock(); }

 private:
  constexpr uptr MmapSize() const {
    return RoundUpTo(kSize2 * sizeof(T), GetPageSizeCached());
  }

  T *Get(uptr idx) const {
    DCHECK_LT(idx, kSize1);
    return reinterpret_cast<T *>(
        atomic_load(&map1_[idx], memory_order_acquire));
  }

  T *GetOrCreate(uptr idx) const {
    DCHECK_LT(idx, kSize1);
    // This code needs to use memory_order_acquire/consume, but we use
    // memory_order_relaxed for performance reasons (matters for arm64). We
    // expect memory_order_relaxed to be effectively equivalent to
    // memory_order_consume in this case for all relevant architectures: all
    // dependent data is reachable only by dereferencing the resulting pointer.
    // If relaxed load fails to see stored ptr, the code will fall back to
    // Create() and reload the value again with locked mutex as a memory
    // barrier.
    T *res = reinterpret_cast<T *>(atomic_load_relaxed(&map1_[idx]));
    if (LIKELY(res))
      return res;
    return Create(idx);
  }

  NOINLINE T *Create(uptr idx) const {
    SpinMutexLock l(&mu_);
    T *res = Get(idx);
    if (!res) {
      res = reinterpret_cast<T *>(MmapOrDie(MmapSize(), "TwoLevelMap"));
      atomic_store(&map1_[idx], reinterpret_cast<uptr>(res),
                   memory_order_release);
    }
    return res;
  }

  mutable StaticSpinMutex mu_;
  mutable atomic_uintptr_t map1_[kSize1];
};

template <u64 kSize, typename AddressSpaceViewTy = LocalAddressSpaceView>
using FlatByteMap = FlatMap<u8, kSize, AddressSpaceViewTy>;

template <u64 kSize1, u64 kSize2,
          typename AddressSpaceViewTy = LocalAddressSpaceView>
using TwoLevelByteMap = TwoLevelMap<u8, kSize1, kSize2, AddressSpaceViewTy>;
}  // namespace __sanitizer

#endif
