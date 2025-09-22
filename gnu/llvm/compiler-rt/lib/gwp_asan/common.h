//===-- common.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This file contains code that is common between the crash handler and the
// GuardedPoolAllocator.

#ifndef GWP_ASAN_COMMON_H_
#define GWP_ASAN_COMMON_H_

#include "gwp_asan/definitions.h"
#include "gwp_asan/options.h"

#include <stddef.h>
#include <stdint.h>

namespace gwp_asan {

// Magic header that resides in the AllocatorState so that GWP-ASan bugreports
// can be understood by tools at different versions. Out-of-process crash
// handlers, like crashpad on Fuchsia, take the raw contents of the
// AllocationMetatada array and the AllocatorState, and shove them into the
// minidump. Online unpacking of these structs needs to know from which version
// of GWP-ASan it's extracting the information, as the structures are not
// stable.
struct AllocatorVersionMagic {
  // The values are copied into the structure at runtime, during
  // `GuardedPoolAllocator::init()` so that GWP-ASan remains completely in the
  // `.bss` segment.
  static constexpr uint8_t kAllocatorVersionMagic[4] = {'A', 'S', 'A', 'N'};
  uint8_t Magic[4] = {};
  // Update the version number when the AllocatorState or AllocationMetadata
  // change.
  static constexpr uint16_t kAllocatorVersion = 2;
  uint16_t Version = 0;
  uint16_t Reserved = 0;
};

enum class Error : uint8_t {
  UNKNOWN,
  USE_AFTER_FREE,
  DOUBLE_FREE,
  INVALID_FREE,
  BUFFER_OVERFLOW,
  BUFFER_UNDERFLOW
};

const char *ErrorToString(const Error &E);

static constexpr uint64_t kInvalidThreadID = UINT64_MAX;
// Get the current thread ID, or kInvalidThreadID if failure. Note: This
// implementation is platform-specific.
uint64_t getThreadID();

// This struct contains all the metadata recorded about a single allocation made
// by GWP-ASan. If `AllocationMetadata.Addr` is zero, the metadata is non-valid.
struct AllocationMetadata {
  // The number of bytes used to store a compressed stack frame. On 64-bit
  // platforms, assuming a compression ratio of 50%, this should allow us to
  // store ~64 frames per trace.
  static constexpr size_t kStackFrameStorageBytes = 256;

  // Maximum number of stack frames to collect on allocation/deallocation. The
  // actual number of collected frames may be less than this as the stack
  // frames are compressed into a fixed memory range.
  static constexpr size_t kMaxTraceLengthToCollect = 128;

  // Records the given allocation metadata into this struct.
  void RecordAllocation(uintptr_t Addr, size_t RequestedSize);
  // Record that this allocation is now deallocated.
  void RecordDeallocation();

  struct CallSiteInfo {
    // Record the current backtrace to this callsite.
    void RecordBacktrace(options::Backtrace_t Backtrace);

    // The compressed backtrace to the allocation/deallocation.
    uint8_t CompressedTrace[kStackFrameStorageBytes];
    // The thread ID for this trace, or kInvalidThreadID if not available.
    uint64_t ThreadID = kInvalidThreadID;
    // The size of the compressed trace (in bytes). Zero indicates that no
    // trace was collected.
    size_t TraceSize = 0;
  };

  // The address of this allocation. If zero, the rest of this struct isn't
  // valid, as the allocation has never occurred.
  uintptr_t Addr = 0;
  // Represents the actual size of the allocation.
  size_t RequestedSize = 0;

  CallSiteInfo AllocationTrace;
  CallSiteInfo DeallocationTrace;

  // Whether this allocation has been deallocated yet.
  bool IsDeallocated = false;

  // In recoverable mode, whether this allocation has had a crash associated
  // with it. This has certain side effects, like meaning this allocation will
  // permanently occupy a slot, and won't ever have another crash reported from
  // it.
  bool HasCrashed = false;
};

// This holds the state that's shared between the GWP-ASan allocator and the
// crash handler. This, in conjunction with the Metadata array, forms the entire
// set of information required for understanding a GWP-ASan crash.
struct AllocatorState {
  constexpr AllocatorState() {}
  AllocatorVersionMagic VersionMagic{};

  // Returns whether the provided pointer is a current sampled allocation that
  // is owned by this pool.
  GWP_ASAN_ALWAYS_INLINE bool pointerIsMine(const void *Ptr) const {
    uintptr_t P = reinterpret_cast<uintptr_t>(Ptr);
    return P < GuardedPagePoolEnd && GuardedPagePool <= P;
  }

  // Returns the address of the N-th guarded slot.
  uintptr_t slotToAddr(size_t N) const;

  // Returns the largest allocation that is supported by this pool.
  size_t maximumAllocationSize() const;

  // Gets the nearest slot to the provided address.
  size_t getNearestSlot(uintptr_t Ptr) const;

  // Returns whether the provided pointer is a guard page or not. The pointer
  // must be within memory owned by this pool, else the result is undefined.
  bool isGuardPage(uintptr_t Ptr) const;

  // Returns the address that's used by __gwp_asan_get_internal_crash_address()
  // and GPA::raiseInternallyDetectedError() to communicate that the SEGV in
  // question comes from an internally-detected error.
  uintptr_t internallyDetectedErrorFaultAddress() const;

  // The number of guarded slots that this pool holds.
  size_t MaxSimultaneousAllocations = 0;

  // Pointer to the pool of guarded slots. Note that this points to the start of
  // the pool (which is a guard page), not a pointer to the first guarded page.
  uintptr_t GuardedPagePool = 0;
  uintptr_t GuardedPagePoolEnd = 0;

  // Cached page size for this system in bytes.
  size_t PageSize = 0;

  // The type and address of an internally-detected failure. For INVALID_FREE
  // and DOUBLE_FREE, these errors are detected in GWP-ASan, which will set
  // these values and terminate the process.
  Error FailureType = Error::UNKNOWN;
  uintptr_t FailureAddress = 0;
};

// Below are various compile-time checks that the layout of the internal
// GWP-ASan structures are undisturbed. If they are disturbed, the version magic
// number needs to be increased by one, and the asserts need to be updated.
// Out-of-process crash handlers, like breakpad/crashpad, may copy the internal
// GWP-ASan structures into a minidump for offline reconstruction of the crash.
// In order to accomplish this, the offline reconstructor needs to know the
// version of GWP-ASan internal structures that it's unpacking (along with the
// architecture-specific layout info, which is left as an exercise to the crash
// handler).
static_assert(offsetof(AllocatorState, VersionMagic) == 0, "");
static_assert(sizeof(AllocatorVersionMagic) == 8, "");
#if defined(__x86_64__)
static_assert(sizeof(AllocatorState) == 56, "");
static_assert(offsetof(AllocatorState, FailureAddress) == 48, "");
static_assert(sizeof(AllocationMetadata) == 568, "");
static_assert(offsetof(AllocationMetadata, IsDeallocated) == 560, "");
#elif defined(__aarch64__)
static_assert(sizeof(AllocatorState) == 56, "");
static_assert(offsetof(AllocatorState, FailureAddress) == 48, "");
static_assert(sizeof(AllocationMetadata) == 568, "");
static_assert(offsetof(AllocationMetadata, IsDeallocated) == 560, "");
#elif defined(__i386__)
static_assert(sizeof(AllocatorState) == 32, "");
static_assert(offsetof(AllocatorState, FailureAddress) == 28, "");
static_assert(sizeof(AllocationMetadata) == 548, "");
static_assert(offsetof(AllocationMetadata, IsDeallocated) == 544, "");
#elif defined(__arm__)
static_assert(sizeof(AllocatorState) == 32, "");
static_assert(offsetof(AllocatorState, FailureAddress) == 28, "");
static_assert(sizeof(AllocationMetadata) == 560, "");
static_assert(offsetof(AllocationMetadata, IsDeallocated) == 552, "");
#endif // defined($ARCHITECTURE)

} // namespace gwp_asan
#endif // GWP_ASAN_COMMON_H_
