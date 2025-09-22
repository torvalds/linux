//===-FrameHeaderCache.hpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Cache the elf program headers necessary to unwind the stack more efficiently
// in the presence of many dsos.
//
//===----------------------------------------------------------------------===//

#ifndef __FRAMEHEADER_CACHE_HPP__
#define __FRAMEHEADER_CACHE_HPP__

#include "config.h"
#include <limits.h>

#ifdef _LIBUNWIND_DEBUG_FRAMEHEADER_CACHE
#define _LIBUNWIND_FRAMEHEADERCACHE_TRACE0(x) _LIBUNWIND_LOG0(x)
#define _LIBUNWIND_FRAMEHEADERCACHE_TRACE(msg, ...)                            \
  _LIBUNWIND_LOG(msg, __VA_ARGS__)
#else
#define _LIBUNWIND_FRAMEHEADERCACHE_TRACE0(x)
#define _LIBUNWIND_FRAMEHEADERCACHE_TRACE(msg, ...)
#endif

// This cache should only be be used from within a dl_iterate_phdr callback.
// dl_iterate_phdr does the necessary synchronization to prevent problems
// with concurrent access via the libc load lock. Adding synchronization
// for other uses is possible, but not currently done.

class _LIBUNWIND_HIDDEN FrameHeaderCache {
  struct CacheEntry {
    uintptr_t LowPC() { return Info.dso_base; }
    uintptr_t HighPC() { return Info.dso_base + Info.text_segment_length; }
    UnwindInfoSections Info;
    CacheEntry *Next;
  };

  static const size_t kCacheEntryCount = 8;

  // Can't depend on the C++ standard library in libunwind, so use an array to
  // allocate the entries, and two linked lists for ordering unused and recently
  // used entries.  FIXME: Would the extra memory for a doubly-linked list
  // be better than the runtime cost of traversing a very short singly-linked
  // list on a cache miss? The entries themselves are all small and consecutive,
  // so unlikely to cause page faults when following the pointers. The memory
  // spent on additional pointers could also be spent on more entries.

  CacheEntry Entries[kCacheEntryCount];
  CacheEntry *MostRecentlyUsed;
  CacheEntry *Unused;

  void resetCache() {
    _LIBUNWIND_FRAMEHEADERCACHE_TRACE0("FrameHeaderCache reset");
    MostRecentlyUsed = nullptr;
    Unused = &Entries[0];
    for (size_t i = 0; i < kCacheEntryCount - 1; i++) {
      Entries[i].Next = &Entries[i + 1];
    }
    Entries[kCacheEntryCount - 1].Next = nullptr;
  }

  bool cacheNeedsReset(dl_phdr_info *PInfo) {
    // C libraries increment dl_phdr_info.adds and dl_phdr_info.subs when
    // loading and unloading shared libraries. If these values change between
    // iterations of dl_iterate_phdr, then invalidate the cache.

    // These are static to avoid needing an initializer, and unsigned long long
    // because that is their type within the extended dl_phdr_info.  Initialize
    // these to something extremely unlikely to be found upon the first call to
    // dl_iterate_phdr.
    static unsigned long long LastAdds = ULLONG_MAX;
    static unsigned long long LastSubs = ULLONG_MAX;
    if (PInfo->dlpi_adds != LastAdds || PInfo->dlpi_subs != LastSubs) {
      // Resetting the entire cache is a big hammer, but this path is rare--
      // usually just on the very first call, when the cache is empty anyway--so
      // added complexity doesn't buy much.
      LastAdds = PInfo->dlpi_adds;
      LastSubs = PInfo->dlpi_subs;
      resetCache();
      return true;
    }
    return false;
  }

public:
  bool find(dl_phdr_info *PInfo, size_t, void *data) {
    if (cacheNeedsReset(PInfo) || MostRecentlyUsed == nullptr)
      return false;

    auto *CBData = static_cast<dl_iterate_cb_data *>(data);
    CacheEntry *Current = MostRecentlyUsed;
    CacheEntry *Previous = nullptr;
    while (Current != nullptr) {
      _LIBUNWIND_FRAMEHEADERCACHE_TRACE(
          "FrameHeaderCache check %lx in [%lx - %lx)", CBData->targetAddr,
          Current->LowPC(), Current->HighPC());
      if (Current->LowPC() <= CBData->targetAddr &&
          CBData->targetAddr < Current->HighPC()) {
        _LIBUNWIND_FRAMEHEADERCACHE_TRACE(
            "FrameHeaderCache hit %lx in [%lx - %lx)", CBData->targetAddr,
            Current->LowPC(), Current->HighPC());
        if (Previous) {
          // If there is no Previous, then Current is already the
          // MostRecentlyUsed, and no need to move it up.
          Previous->Next = Current->Next;
          Current->Next = MostRecentlyUsed;
          MostRecentlyUsed = Current;
        }
        *CBData->sects = Current->Info;
        return true;
      }
      Previous = Current;
      Current = Current->Next;
    }
    _LIBUNWIND_FRAMEHEADERCACHE_TRACE("FrameHeaderCache miss for address %lx",
                                      CBData->targetAddr);
    return false;
  }

  void add(const UnwindInfoSections *UIS) {
    CacheEntry *Current = nullptr;

    if (Unused != nullptr) {
      Current = Unused;
      Unused = Unused->Next;
    } else {
      Current = MostRecentlyUsed;
      CacheEntry *Previous = nullptr;
      while (Current->Next != nullptr) {
        Previous = Current;
        Current = Current->Next;
      }
      Previous->Next = nullptr;
      _LIBUNWIND_FRAMEHEADERCACHE_TRACE("FrameHeaderCache evict [%lx - %lx)",
                                        Current->LowPC(), Current->HighPC());
    }

    Current->Info = *UIS;
    Current->Next = MostRecentlyUsed;
    MostRecentlyUsed = Current;
    _LIBUNWIND_FRAMEHEADERCACHE_TRACE("FrameHeaderCache add [%lx - %lx)",
                                      MostRecentlyUsed->LowPC(),
                                      MostRecentlyUsed->HighPC());
  }
};

#endif // __FRAMEHEADER_CACHE_HPP__
