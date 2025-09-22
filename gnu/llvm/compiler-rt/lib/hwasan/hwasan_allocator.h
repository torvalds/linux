//===-- hwasan_allocator.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_ALLOCATOR_H
#define HWASAN_ALLOCATOR_H

#include "hwasan.h"
#include "hwasan_interface_internal.h"
#include "hwasan_mapping.h"
#include "hwasan_poisoning.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_ring_buffer.h"

#if !defined(__aarch64__) && !defined(__x86_64__) && !(SANITIZER_RISCV64)
#  error Unsupported platform
#endif

namespace __hwasan {

struct Metadata {
 private:
  atomic_uint64_t alloc_context_id;
  u32 requested_size_low;
  u16 requested_size_high;
  atomic_uint8_t chunk_state;
  u8 lsan_tag;

 public:
  inline void SetAllocated(u32 stack, u64 size);
  inline void SetUnallocated();

  inline bool IsAllocated() const;
  inline u64 GetRequestedSize() const;
  inline u32 GetAllocStackId() const;
  inline u32 GetAllocThreadId() const;
  inline void SetLsanTag(__lsan::ChunkTag tag);
  inline __lsan::ChunkTag GetLsanTag() const;
};
static_assert(sizeof(Metadata) == 16);

struct HwasanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const { UpdateMemoryUsage(); }
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {
    UpdateMemoryUsage();
  }
  void OnUnmap(uptr p, uptr size) const {
    // We are about to unmap a chunk of user memory.
    // It can return as user-requested mmap() or another thread stack.
    // Make it accessible with zero-tagged pointer.
    TagMemory(p, size, 0);
  }
};

static const uptr kMaxAllowedMallocSize = 1UL << 40;  // 1T

struct AP64 {
  static const uptr kSpaceBeg = ~0ULL;

#if defined(HWASAN_ALIASING_MODE)
  static const uptr kSpaceSize = 1ULL << kAddressTagShift;
  typedef __sanitizer::DefaultSizeClassMap SizeClassMap;
#elif SANITIZER_LINUX && !SANITIZER_ANDROID
  static const uptr kSpaceSize = 0x40000000000ULL;  // 4T.
  typedef __sanitizer::DefaultSizeClassMap SizeClassMap;
#else
  static const uptr kSpaceSize = 0x2000000000ULL;  // 128G.
  typedef __sanitizer::VeryDenseSizeClassMap SizeClassMap;
#endif

  static const uptr kMetadataSize = sizeof(Metadata);
  using AddressSpaceView = LocalAddressSpaceView;
  typedef HwasanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

void AllocatorThreadStart(AllocatorCache *cache);
void AllocatorThreadFinish(AllocatorCache *cache);

class HwasanChunkView {
 public:
  HwasanChunkView() : block_(0), metadata_(nullptr) {}
  HwasanChunkView(uptr block, Metadata *metadata)
      : block_(block), metadata_(metadata) {}
  bool IsAllocated() const;    // Checks if the memory is currently allocated
  uptr Beg() const;            // First byte of user memory
  uptr End() const;            // Last byte of user memory
  uptr UsedSize() const;       // Size requested by the user
  uptr ActualSize() const;     // Size allocated by the allocator.
  u32 GetAllocStackId() const;
  u32 GetAllocThreadId() const;
  bool FromSmallHeap() const;
  bool AddrIsInside(uptr addr) const;

 private:
  friend class __lsan::LsanMetadata;
  uptr block_;
  Metadata *const metadata_;
};

HwasanChunkView FindHeapChunkByAddress(uptr address);

// Information about one (de)allocation that happened in the past.
// These are recorded in a thread-local ring buffer.
struct HeapAllocationRecord {
  uptr tagged_addr;
  u32 alloc_thread_id;
  u32 alloc_context_id;
  u32 free_context_id;
  u32 requested_size;
};

typedef RingBuffer<HeapAllocationRecord> HeapAllocationsRingBuffer;

void GetAllocatorStats(AllocatorStatCounters s);

} // namespace __hwasan

#endif // HWASAN_ALLOCATOR_H
