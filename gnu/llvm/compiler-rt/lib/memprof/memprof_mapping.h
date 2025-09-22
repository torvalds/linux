//===-- memprof_mapping.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Defines MemProf memory mapping.
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_MAPPING_H
#define MEMPROF_MAPPING_H

#include "memprof_internal.h"

static const u64 kDefaultShadowScale = 3;
#define SHADOW_SCALE kDefaultShadowScale

#define SHADOW_OFFSET __memprof_shadow_memory_dynamic_address

#define SHADOW_GRANULARITY (1ULL << SHADOW_SCALE)
#define MEMPROF_ALIGNMENT 32
namespace __memprof {

extern uptr kHighMemEnd; // Initialized in __memprof_init.

} // namespace __memprof

// Size of memory block mapped to a single shadow location
#define MEM_GRANULARITY 64ULL

#define SHADOW_MASK ~(MEM_GRANULARITY - 1)

#define MEM_TO_SHADOW(mem)                                                     \
  ((((mem) & SHADOW_MASK) >> SHADOW_SCALE) + (SHADOW_OFFSET))

// Histogram shadow memory is laid different to the standard configuration:

//             8 bytes
//         +---+---+---+  +---+---+---+  +---+---+---+
//  Memory |     a     |  |     b     |  |     c     |
//         +---+---+---+  +---+---+---+  +---+---+---+

//             +---+          +---+          +---+
//  Shadow     | a |          | b |          | c |
//             +---+          +---+          +---+
//            1 byte
//
// Where we have a 1 byte counter for each 8 bytes. HISTOGRAM_MEM_TO_SHADOW
// translates a memory address to the address of its corresponding shadow
// counter memory address. The same data is still provided in MIB whether
// histograms are used or not. Total access counts per allocations are
// computed by summing up all individual 1 byte counters. This can incur an
// accuracy penalty.

#define HISTOGRAM_GRANULARITY 8U

#define HISTOGRAM_MAX_COUNTER 255U

#define HISTOGRAM_SHADOW_MASK ~(HISTOGRAM_GRANULARITY - 1)

#define HISTOGRAM_MEM_TO_SHADOW(mem)                                           \
  ((((mem) & HISTOGRAM_SHADOW_MASK) >> SHADOW_SCALE) + (SHADOW_OFFSET))

#define SHADOW_ENTRY_SIZE (MEM_GRANULARITY >> SHADOW_SCALE)

#define kLowMemBeg 0
#define kLowMemEnd (SHADOW_OFFSET ? SHADOW_OFFSET - 1 : 0)

#define kLowShadowBeg SHADOW_OFFSET
#define kLowShadowEnd (MEM_TO_SHADOW(kLowMemEnd) + SHADOW_ENTRY_SIZE - 1)

#define kHighMemBeg (MEM_TO_SHADOW(kHighMemEnd) + 1 + SHADOW_ENTRY_SIZE - 1)

#define kHighShadowBeg MEM_TO_SHADOW(kHighMemBeg)
#define kHighShadowEnd (MEM_TO_SHADOW(kHighMemEnd) + SHADOW_ENTRY_SIZE - 1)

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
#define kZeroBaseShadowStart 0
#define kZeroBaseMaxShadowStart (1 << 18)

#define kShadowGapBeg (kLowShadowEnd ? kLowShadowEnd + 1 : kZeroBaseShadowStart)
#define kShadowGapEnd (kHighShadowBeg - 1)

namespace __memprof {

inline uptr MemToShadowSize(uptr size) { return size >> SHADOW_SCALE; }
inline bool AddrIsInLowMem(uptr a) { return a <= kLowMemEnd; }

inline bool AddrIsInLowShadow(uptr a) {
  return a >= kLowShadowBeg && a <= kLowShadowEnd;
}

inline bool AddrIsInHighMem(uptr a) {
  return kHighMemBeg && a >= kHighMemBeg && a <= kHighMemEnd;
}

inline bool AddrIsInHighShadow(uptr a) {
  return kHighMemBeg && a >= kHighShadowBeg && a <= kHighShadowEnd;
}

inline bool AddrIsInShadowGap(uptr a) {
  // In zero-based shadow mode we treat addresses near zero as addresses
  // in shadow gap as well.
  if (SHADOW_OFFSET == 0)
    return a <= kShadowGapEnd;
  return a >= kShadowGapBeg && a <= kShadowGapEnd;
}

inline bool AddrIsInMem(uptr a) {
  return AddrIsInLowMem(a) || AddrIsInHighMem(a) ||
         (flags()->protect_shadow_gap == 0 && AddrIsInShadowGap(a));
}

inline uptr MemToShadow(uptr p) {
  CHECK(AddrIsInMem(p));
  return MEM_TO_SHADOW(p);
}

inline bool AddrIsInShadow(uptr a) {
  return AddrIsInLowShadow(a) || AddrIsInHighShadow(a);
}

inline bool AddrIsAlignedByGranularity(uptr a) {
  return (a & (SHADOW_GRANULARITY - 1)) == 0;
}

inline void RecordAccess(uptr a) {
  // If we use a different shadow size then the type below needs adjustment.
  CHECK_EQ(SHADOW_ENTRY_SIZE, 8);
  u64 *shadow_address = (u64 *)MEM_TO_SHADOW(a);
  (*shadow_address)++;
}

inline void RecordAccessHistogram(uptr a) {
  CHECK_EQ(SHADOW_ENTRY_SIZE, 8);
  u8 *shadow_address = (u8 *)HISTOGRAM_MEM_TO_SHADOW(a);
  if (*shadow_address < HISTOGRAM_MAX_COUNTER) {
    (*shadow_address)++;
  }
}

} // namespace __memprof

#endif // MEMPROF_MAPPING_H
