//===-- wrappers_c.inc ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PREFIX
#error "Define SCUDO_PREFIX prior to including this file!"
#endif

// malloc-type functions have to be aligned to std::max_align_t. This is
// distinct from (1U << SCUDO_MIN_ALIGNMENT_LOG), since C++ new-type functions
// do not have to abide by the same requirement.
#ifndef SCUDO_MALLOC_ALIGNMENT
#define SCUDO_MALLOC_ALIGNMENT FIRST_32_SECOND_64(8U, 16U)
#endif

static void reportAllocation(void *ptr, size_t size) {
  if (SCUDO_ENABLE_HOOKS)
    if (__scudo_allocate_hook && ptr)
      __scudo_allocate_hook(ptr, size);
}
static void reportDeallocation(void *ptr) {
  if (SCUDO_ENABLE_HOOKS)
    if (__scudo_deallocate_hook)
      __scudo_deallocate_hook(ptr);
}
static void reportReallocAllocation(void *old_ptr, void *new_ptr, size_t size) {
  DCHECK_NE(new_ptr, nullptr);

  if (SCUDO_ENABLE_HOOKS) {
    if (__scudo_realloc_allocate_hook)
      __scudo_realloc_allocate_hook(old_ptr, new_ptr, size);
    else if (__scudo_allocate_hook)
      __scudo_allocate_hook(new_ptr, size);
  }
}
static void reportReallocDeallocation(void *old_ptr) {
  if (SCUDO_ENABLE_HOOKS) {
    if (__scudo_realloc_deallocate_hook)
      __scudo_realloc_deallocate_hook(old_ptr);
    else if (__scudo_deallocate_hook)
      __scudo_deallocate_hook(old_ptr);
  }
}

extern "C" {

INTERFACE WEAK void *SCUDO_PREFIX(calloc)(size_t nmemb, size_t size) {
  scudo::uptr Product;
  if (UNLIKELY(scudo::checkForCallocOverflow(size, nmemb, &Product))) {
    if (SCUDO_ALLOCATOR.canReturnNull()) {
      errno = ENOMEM;
      return nullptr;
    }
    scudo::reportCallocOverflow(nmemb, size);
  }
  void *Ptr = SCUDO_ALLOCATOR.allocate(Product, scudo::Chunk::Origin::Malloc,
                                       SCUDO_MALLOC_ALIGNMENT, true);
  reportAllocation(Ptr, Product);
  return scudo::setErrnoOnNull(Ptr);
}

INTERFACE WEAK void SCUDO_PREFIX(free)(void *ptr) {
  reportDeallocation(ptr);
  SCUDO_ALLOCATOR.deallocate(ptr, scudo::Chunk::Origin::Malloc);
}

INTERFACE WEAK struct SCUDO_MALLINFO SCUDO_PREFIX(mallinfo)(void) {
  struct SCUDO_MALLINFO Info = {};
  scudo::StatCounters Stats;
  SCUDO_ALLOCATOR.getStats(Stats);
  // Space allocated in mmapped regions (bytes)
  Info.hblkhd = static_cast<__scudo_mallinfo_data_t>(Stats[scudo::StatMapped]);
  // Maximum total allocated space (bytes)
  Info.usmblks = Info.hblkhd;
  // Space in freed fastbin blocks (bytes)
  Info.fsmblks = static_cast<__scudo_mallinfo_data_t>(Stats[scudo::StatFree]);
  // Total allocated space (bytes)
  Info.uordblks =
      static_cast<__scudo_mallinfo_data_t>(Stats[scudo::StatAllocated]);
  // Total free space (bytes)
  Info.fordblks = Info.fsmblks;
  return Info;
}

// On Android, mallinfo2 is an alias of mallinfo, so don't define both.
#if !SCUDO_ANDROID
INTERFACE WEAK struct __scudo_mallinfo2 SCUDO_PREFIX(mallinfo2)(void) {
  struct __scudo_mallinfo2 Info = {};
  scudo::StatCounters Stats;
  SCUDO_ALLOCATOR.getStats(Stats);
  // Space allocated in mmapped regions (bytes)
  Info.hblkhd = Stats[scudo::StatMapped];
  // Maximum total allocated space (bytes)
  Info.usmblks = Info.hblkhd;
  // Space in freed fastbin blocks (bytes)
  Info.fsmblks = Stats[scudo::StatFree];
  // Total allocated space (bytes)
  Info.uordblks = Stats[scudo::StatAllocated];
  // Total free space (bytes)
  Info.fordblks = Info.fsmblks;
  return Info;
}
#endif

INTERFACE WEAK void *SCUDO_PREFIX(malloc)(size_t size) {
  void *Ptr = SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Malloc,
                                       SCUDO_MALLOC_ALIGNMENT);
  reportAllocation(Ptr, size);
  return scudo::setErrnoOnNull(Ptr);
}

#if SCUDO_ANDROID
INTERFACE WEAK size_t SCUDO_PREFIX(malloc_usable_size)(const void *ptr) {
#else
INTERFACE WEAK size_t SCUDO_PREFIX(malloc_usable_size)(void *ptr) {
#endif
  return SCUDO_ALLOCATOR.getUsableSize(ptr);
}

INTERFACE WEAK void *SCUDO_PREFIX(memalign)(size_t alignment, size_t size) {
  // Android rounds up the alignment to a power of two if it isn't one.
  if (SCUDO_ANDROID) {
    if (UNLIKELY(!alignment)) {
      alignment = 1U;
    } else {
      if (UNLIKELY(!scudo::isPowerOfTwo(alignment)))
        alignment = scudo::roundUpPowerOfTwo(alignment);
    }
  } else {
    if (UNLIKELY(!scudo::isPowerOfTwo(alignment))) {
      if (SCUDO_ALLOCATOR.canReturnNull()) {
        errno = EINVAL;
        return nullptr;
      }
      scudo::reportAlignmentNotPowerOfTwo(alignment);
    }
  }
  void *Ptr =
      SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Memalign, alignment);
  reportAllocation(Ptr, size);
  return Ptr;
}

INTERFACE WEAK int SCUDO_PREFIX(posix_memalign)(void **memptr, size_t alignment,
                                                size_t size) {
  if (UNLIKELY(scudo::checkPosixMemalignAlignment(alignment))) {
    if (!SCUDO_ALLOCATOR.canReturnNull())
      scudo::reportInvalidPosixMemalignAlignment(alignment);
    return EINVAL;
  }
  void *Ptr =
      SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Memalign, alignment);
  if (UNLIKELY(!Ptr))
    return ENOMEM;
  reportAllocation(Ptr, size);

  *memptr = Ptr;
  return 0;
}

INTERFACE WEAK void *SCUDO_PREFIX(pvalloc)(size_t size) {
  const scudo::uptr PageSize = scudo::getPageSizeCached();
  if (UNLIKELY(scudo::checkForPvallocOverflow(size, PageSize))) {
    if (SCUDO_ALLOCATOR.canReturnNull()) {
      errno = ENOMEM;
      return nullptr;
    }
    scudo::reportPvallocOverflow(size);
  }
  // pvalloc(0) should allocate one page.
  void *Ptr =
      SCUDO_ALLOCATOR.allocate(size ? scudo::roundUp(size, PageSize) : PageSize,
                               scudo::Chunk::Origin::Memalign, PageSize);
  reportAllocation(Ptr, scudo::roundUp(size, PageSize));

  return scudo::setErrnoOnNull(Ptr);
}

INTERFACE WEAK void *SCUDO_PREFIX(realloc)(void *ptr, size_t size) {
  if (!ptr) {
    void *Ptr = SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Malloc,
                                         SCUDO_MALLOC_ALIGNMENT);
    reportAllocation(Ptr, size);
    return scudo::setErrnoOnNull(Ptr);
  }
  if (size == 0) {
    reportDeallocation(ptr);
    SCUDO_ALLOCATOR.deallocate(ptr, scudo::Chunk::Origin::Malloc);
    return nullptr;
  }

  // Given that the reporting of deallocation and allocation are not atomic, we
  // always pretend the old pointer will be released so that the user doesn't
  // need to worry about the false double-use case from the view of hooks.
  //
  // For example, assume that `realloc` releases the old pointer and allocates a
  // new pointer. Before the reporting of both operations has been done, another
  // thread may get the old pointer from `malloc`. It may be misinterpreted as
  // double-use if it's not handled properly on the hook side.
  reportReallocDeallocation(ptr);
  void *NewPtr = SCUDO_ALLOCATOR.reallocate(ptr, size, SCUDO_MALLOC_ALIGNMENT);
  if (NewPtr != nullptr) {
    // Note that even if NewPtr == ptr, the size has changed. We still need to
    // report the new size.
    reportReallocAllocation(/*OldPtr=*/ptr, NewPtr, size);
  } else {
    // If `realloc` fails, the old pointer is not released. Report the old
    // pointer as allocated again.
    reportReallocAllocation(/*OldPtr=*/ptr, /*NewPtr=*/ptr,
                            SCUDO_ALLOCATOR.getAllocSize(ptr));
  }

  return scudo::setErrnoOnNull(NewPtr);
}

INTERFACE WEAK void *SCUDO_PREFIX(valloc)(size_t size) {
  void *Ptr = SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Memalign,
                                       scudo::getPageSizeCached());
  reportAllocation(Ptr, size);

  return scudo::setErrnoOnNull(Ptr);
}

INTERFACE WEAK int SCUDO_PREFIX(malloc_iterate)(
    uintptr_t base, size_t size,
    void (*callback)(uintptr_t base, size_t size, void *arg), void *arg) {
  SCUDO_ALLOCATOR.iterateOverChunks(base, size, callback, arg);
  return 0;
}

INTERFACE WEAK void SCUDO_PREFIX(malloc_enable)() { SCUDO_ALLOCATOR.enable(); }

INTERFACE WEAK void SCUDO_PREFIX(malloc_disable)() {
  SCUDO_ALLOCATOR.disable();
}

void SCUDO_PREFIX(malloc_postinit)() {
  SCUDO_ALLOCATOR.initGwpAsan();
  pthread_atfork(SCUDO_PREFIX(malloc_disable), SCUDO_PREFIX(malloc_enable),
                 SCUDO_PREFIX(malloc_enable));
}

INTERFACE WEAK int SCUDO_PREFIX(mallopt)(int param, int value) {
  if (param == M_DECAY_TIME) {
    if (SCUDO_ANDROID) {
      // Before changing the interval, reset the memory usage status by doing a
      // M_PURGE call so that we can minimize the impact of any unreleased pages
      // introduced by interval transition.
      SCUDO_ALLOCATOR.releaseToOS(scudo::ReleaseToOS::Force);

      // The values allowed on Android are {-1, 0, 1}. "1" means the longest
      // interval.
      CHECK(value >= -1 && value <= 1);
      if (value == 1)
        value = INT32_MAX;
    }

    SCUDO_ALLOCATOR.setOption(scudo::Option::ReleaseInterval,
                              static_cast<scudo::sptr>(value));
    return 1;
  } else if (param == M_PURGE) {
    SCUDO_ALLOCATOR.releaseToOS(scudo::ReleaseToOS::Force);
    return 1;
  } else if (param == M_PURGE_ALL) {
    SCUDO_ALLOCATOR.releaseToOS(scudo::ReleaseToOS::ForceAll);
    return 1;
  } else if (param == M_LOG_STATS) {
    SCUDO_ALLOCATOR.printStats();
    SCUDO_ALLOCATOR.printFragmentationInfo();
    return 1;
  } else {
    scudo::Option option;
    switch (param) {
    case M_MEMTAG_TUNING:
      option = scudo::Option::MemtagTuning;
      break;
    case M_THREAD_DISABLE_MEM_INIT:
      option = scudo::Option::ThreadDisableMemInit;
      break;
    case M_CACHE_COUNT_MAX:
      option = scudo::Option::MaxCacheEntriesCount;
      break;
    case M_CACHE_SIZE_MAX:
      option = scudo::Option::MaxCacheEntrySize;
      break;
    case M_TSDS_COUNT_MAX:
      option = scudo::Option::MaxTSDsCount;
      break;
    default:
      return 0;
    }
    return SCUDO_ALLOCATOR.setOption(option, static_cast<scudo::sptr>(value));
  }
}

INTERFACE WEAK void *SCUDO_PREFIX(aligned_alloc)(size_t alignment,
                                                 size_t size) {
  if (UNLIKELY(scudo::checkAlignedAllocAlignmentAndSize(alignment, size))) {
    if (SCUDO_ALLOCATOR.canReturnNull()) {
      errno = EINVAL;
      return nullptr;
    }
    scudo::reportInvalidAlignedAllocAlignment(alignment, size);
  }

  void *Ptr =
      SCUDO_ALLOCATOR.allocate(size, scudo::Chunk::Origin::Malloc, alignment);
  reportAllocation(Ptr, size);

  return scudo::setErrnoOnNull(Ptr);
}

INTERFACE WEAK int SCUDO_PREFIX(malloc_info)(UNUSED int options, FILE *stream) {
  const scudo::uptr max_size =
      decltype(SCUDO_ALLOCATOR)::PrimaryT::SizeClassMap::MaxSize;
  auto *sizes = static_cast<scudo::uptr *>(
      SCUDO_PREFIX(calloc)(max_size, sizeof(scudo::uptr)));
  auto callback = [](uintptr_t, size_t size, void *arg) {
    auto *sizes = reinterpret_cast<scudo::uptr *>(arg);
    if (size < max_size)
      sizes[size]++;
  };

  SCUDO_ALLOCATOR.disable();
  SCUDO_ALLOCATOR.iterateOverChunks(0, -1ul, callback, sizes);
  SCUDO_ALLOCATOR.enable();

  fputs("<malloc version=\"scudo-1\">\n", stream);
  for (scudo::uptr i = 0; i != max_size; ++i)
    if (sizes[i])
      fprintf(stream, "<alloc size=\"%zu\" count=\"%zu\"/>\n", i, sizes[i]);
  fputs("</malloc>\n", stream);
  SCUDO_PREFIX(free)(sizes);
  return 0;
}

// Disable memory tagging for the heap. The caller must disable memory tag
// checks globally (e.g. by clearing TCF0 on aarch64) before calling this
// function, and may not re-enable them after calling the function.
INTERFACE WEAK void SCUDO_PREFIX(malloc_disable_memory_tagging)() {
  SCUDO_ALLOCATOR.disableMemoryTagging();
}

// Sets whether scudo records stack traces and other metadata for allocations
// and deallocations. This function only has an effect if the allocator and
// hardware support memory tagging.
INTERFACE WEAK void
SCUDO_PREFIX(malloc_set_track_allocation_stacks)(int track) {
  SCUDO_ALLOCATOR.setTrackAllocationStacks(track);
}

// Sets whether scudo zero-initializes all allocated memory.
INTERFACE WEAK void SCUDO_PREFIX(malloc_set_zero_contents)(int zero_contents) {
  SCUDO_ALLOCATOR.setFillContents(zero_contents ? scudo::ZeroFill
                                                : scudo::NoFill);
}

// Sets whether scudo pattern-initializes all allocated memory.
INTERFACE WEAK void
SCUDO_PREFIX(malloc_set_pattern_fill_contents)(int pattern_fill_contents) {
  SCUDO_ALLOCATOR.setFillContents(
      pattern_fill_contents ? scudo::PatternOrZeroFill : scudo::NoFill);
}

// Sets whether scudo adds a small amount of slack at the end of large
// allocations, before the guard page. This can be enabled to work around buggy
// applications that read a few bytes past the end of their allocation.
INTERFACE WEAK void
SCUDO_PREFIX(malloc_set_add_large_allocation_slack)(int add_slack) {
  SCUDO_ALLOCATOR.setAddLargeAllocationSlack(add_slack);
}

} // extern "C"
