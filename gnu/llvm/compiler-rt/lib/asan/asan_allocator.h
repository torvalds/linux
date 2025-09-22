//===-- asan_allocator.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_allocator.cpp.
//===----------------------------------------------------------------------===//

#ifndef ASAN_ALLOCATOR_H
#define ASAN_ALLOCATOR_H

#include "asan_flags.h"
#include "asan_interceptors.h"
#include "asan_internal.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_list.h"
#include "sanitizer_common/sanitizer_platform.h"

namespace __asan {

enum AllocType {
  FROM_MALLOC = 1,  // Memory block came from malloc, calloc, realloc, etc.
  FROM_NEW = 2,     // Memory block came from operator new.
  FROM_NEW_BR = 3   // Memory block came from operator new [ ]
};

class AsanChunk;

struct AllocatorOptions {
  u32 quarantine_size_mb;
  u32 thread_local_quarantine_size_kb;
  u16 min_redzone;
  u16 max_redzone;
  u8 may_return_null;
  u8 alloc_dealloc_mismatch;
  s32 release_to_os_interval_ms;

  void SetFrom(const Flags *f, const CommonFlags *cf);
  void CopyTo(Flags *f, CommonFlags *cf);
};

void InitializeAllocator(const AllocatorOptions &options);
void ReInitializeAllocator(const AllocatorOptions &options);
void GetAllocatorOptions(AllocatorOptions *options);

class AsanChunkView {
 public:
  explicit AsanChunkView(AsanChunk *chunk) : chunk_(chunk) {}
  bool IsValid() const;        // Checks if AsanChunkView points to a valid
                               // allocated or quarantined chunk.
  bool IsAllocated() const;    // Checks if the memory is currently allocated.
  bool IsQuarantined() const;  // Checks if the memory is currently quarantined.
  uptr Beg() const;            // First byte of user memory.
  uptr End() const;            // Last byte of user memory.
  uptr UsedSize() const;       // Size requested by the user.
  u32 UserRequestedAlignment() const;  // Originally requested alignment.
  uptr AllocTid() const;
  uptr FreeTid() const;
  bool Eq(const AsanChunkView &c) const { return chunk_ == c.chunk_; }
  u32 GetAllocStackId() const;
  u32 GetFreeStackId() const;
  AllocType GetAllocType() const;
  bool AddrIsInside(uptr addr, uptr access_size, sptr *offset) const {
    if (addr >= Beg() && (addr + access_size) <= End()) {
      *offset = addr - Beg();
      return true;
    }
    return false;
  }
  bool AddrIsAtLeft(uptr addr, uptr access_size, sptr *offset) const {
    (void)access_size;
    if (addr < Beg()) {
      *offset = Beg() - addr;
      return true;
    }
    return false;
  }
  bool AddrIsAtRight(uptr addr, uptr access_size, sptr *offset) const {
    if (addr + access_size > End()) {
      *offset = addr - End();
      return true;
    }
    return false;
  }

 private:
  AsanChunk *const chunk_;
};

AsanChunkView FindHeapChunkByAddress(uptr address);
AsanChunkView FindHeapChunkByAllocBeg(uptr address);

// List of AsanChunks with total size.
class AsanChunkFifoList: public IntrusiveList<AsanChunk> {
 public:
  explicit AsanChunkFifoList(LinkerInitialized) { }
  AsanChunkFifoList() { clear(); }
  void Push(AsanChunk *n);
  void PushList(AsanChunkFifoList *q);
  AsanChunk *Pop();
  uptr size() { return size_; }
  void clear() {
    IntrusiveList<AsanChunk>::clear();
    size_ = 0;
  }
 private:
  uptr size_;
};

struct AsanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const;
  void OnMapSecondary(uptr p, uptr size, uptr user_begin, uptr user_size) const;
  void OnUnmap(uptr p, uptr size) const;
};

#if SANITIZER_CAN_USE_ALLOCATOR64
# if SANITIZER_FUCHSIA
// This is a sentinel indicating we do not want the primary allocator arena to
// be placed at a fixed address. It will be anonymously mmap'd.
const uptr kAllocatorSpace = ~(uptr)0;
#    if SANITIZER_RISCV64

// These are sanitizer tunings that allow all bringup tests for RISCV-64 Sv39 +
// Fuchsia to run with asan-instrumented. That is, we can run bringup, e2e,
// libc, and scudo tests with this configuration.
//
// TODO: This is specifically tuned for Sv39. 48/57 will likely require other
// tunings, or possibly use the same tunings Fuchsia uses for other archs. The
// VMA size isn't technically tied to the Fuchsia System ABI, so once 48/57 is
// supported, we'd need a way of dynamically checking what the VMA size is and
// determining optimal configuration.

// This indicates the total amount of space dedicated for the primary allocator
// during initialization. This is roughly proportional to the size set by the
// FuchsiaConfig for scudo (~11.25GB == ~2^33.49). Requesting any more could
// lead to some failures in sanitized bringup tests where we can't allocate new
// vmars because there wouldn't be enough contiguous space. We could try 2^34 if
// we re-evaluate the SizeClassMap settings.
const uptr kAllocatorSize = UINT64_C(1) << 33;  // 8GB

// This is roughly equivalent to the configuration for the VeryDenseSizeClassMap
// but has fewer size classes (ideally at most 32). Fewer class sizes means the
// region size for each class is larger, thus less chances of running out of
// space for each region. The main differences are the MidSizeLog (which is
// smaller) and the MaxSizeLog (which is larger).
//
// - The MaxSizeLog is higher to allow some of the largest allocations I've
//   observed to be placed in the primary allocator's arena as opposed to being
//   mmap'd by the secondary allocator. This helps reduce fragmentation from
//   large classes. A huge example of this the scudo allocator tests (and its
//   testing infrastructure) which malloc's/new's objects on the order of
//   hundreds of kilobytes which normally would not be in the primary allocator
//   arena with the default VeryDenseSizeClassMap.
// - The MidSizeLog is reduced to help shrink the number of size classes and
//   increase region size. Without this, we'd see ASan complain many times about
//   a region running out of available space.
//
// This differs a bit from the fuchsia config in scudo, mainly from the NumBits,
// MaxSizeLog, and NumCachedHintT. This should place the number of size classes
// for scudo at 45 and some large objects allocated by this config would be
// placed in the arena whereas scudo would mmap them. The asan allocator needs
// to have a number of classes that are a power of 2 for various internal things
// to work, so we can't match the scudo settings to a tee. The sanitizer
// allocator is slightly slower than scudo's but this is enough to get
// memory-intensive scudo tests to run with asan instrumentation.
typedef SizeClassMap</*kNumBits=*/2,
                     /*kMinSizeLog=*/5,
                     /*kMidSizeLog=*/8,
                     /*kMaxSizeLog=*/18,
                     /*kNumCachedHintT=*/8,
                     /*kMaxBytesCachedLog=*/10>
    SizeClassMap;
static_assert(SizeClassMap::kNumClassesRounded <= 32,
              "The above tunings were specifically selected to ensure there "
              "would be at most 32 size classes. This restriction could be "
              "loosened to 64 size classes if we can find a configuration of "
              "allocator size and SizeClassMap tunings that allows us to "
              "reliably run all bringup tests in a sanitized environment.");

#    else
// These are the default allocator tunings for non-RISCV environments where the
// VMA is usually 48 bits and we have lots of space.
const uptr kAllocatorSize = 0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
#    endif
#  elif defined(__powerpc64__)
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
#  elif defined(__aarch64__) && SANITIZER_ANDROID
// Android needs to support 39, 42 and 48 bit VMA.
const uptr kAllocatorSpace =  ~(uptr)0;
const uptr kAllocatorSize  =  0x2000000000ULL;  // 128G.
typedef VeryCompactSizeClassMap SizeClassMap;
#  elif SANITIZER_RISCV64
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize = 0x2000000000ULL;  // 128G.
typedef VeryDenseSizeClassMap SizeClassMap;
#  elif defined(__sparc__)
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize = 0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
#  elif SANITIZER_WINDOWS
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x8000000000ULL;  // 500G
typedef DefaultSizeClassMap SizeClassMap;
#  elif SANITIZER_APPLE
const uptr kAllocatorSpace = 0x600000000000ULL;
const uptr kAllocatorSize  =  0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
#  else
const uptr kAllocatorSpace = 0x500000000000ULL;
const uptr kAllocatorSize = 0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
#  endif
template <typename AddressSpaceViewTy>
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef __asan::SizeClassMap SizeClassMap;
  typedef AsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator64<AP64<AddressSpaceView>>;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#else  // Fallback to SizeClassAllocator32.
typedef CompactSizeClassMap SizeClassMap;
template <typename AddressSpaceViewTy>
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 0;
  typedef __asan::SizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = 20;
  using AddressSpaceView = AddressSpaceViewTy;
  typedef AsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator32<AP32<AddressSpaceView> >;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

static const uptr kNumberOfSizeClasses = SizeClassMap::kNumClasses;

template <typename AddressSpaceView>
using AsanAllocatorASVT =
    CombinedAllocator<PrimaryAllocatorASVT<AddressSpaceView>>;
using AsanAllocator = AsanAllocatorASVT<LocalAddressSpaceView>;
using AllocatorCache = AsanAllocator::AllocatorCache;

struct AsanThreadLocalMallocStorage {
  uptr quarantine_cache[16];
  AllocatorCache allocator_cache;
  void CommitBack();
 private:
  // These objects are allocated via mmap() and are zero-initialized.
  AsanThreadLocalMallocStorage() {}
};

void *asan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                    AllocType alloc_type);
void asan_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type);
void asan_delete(void *ptr, uptr size, uptr alignment,
                 BufferedStackTrace *stack, AllocType alloc_type);

void *asan_malloc(uptr size, BufferedStackTrace *stack);
void *asan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack);
void *asan_realloc(void *p, uptr size, BufferedStackTrace *stack);
void *asan_reallocarray(void *p, uptr nmemb, uptr size,
                        BufferedStackTrace *stack);
void *asan_valloc(uptr size, BufferedStackTrace *stack);
void *asan_pvalloc(uptr size, BufferedStackTrace *stack);

void *asan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack);
int asan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack);
uptr asan_malloc_usable_size(const void *ptr, uptr pc, uptr bp);

uptr asan_mz_size(const void *ptr);
void asan_mz_force_lock();
void asan_mz_force_unlock();

void PrintInternalAllocatorStats();
void AsanSoftRssLimitExceededCallback(bool exceeded);

}  // namespace __asan
#endif  // ASAN_ALLOCATOR_H
