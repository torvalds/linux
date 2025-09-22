//===-- asan_mapping_sparc64.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// SPARC64-specific definitions for ASan memory mapping.
//===----------------------------------------------------------------------===//
#ifndef ASAN_MAPPING_SPARC64_H
#define ASAN_MAPPING_SPARC64_H

// This is tailored to the 52-bit VM layout on SPARC-T4 and later.
// The VM space is split into two 51-bit halves at both ends: the low part
// has all the bits above the 51st cleared, while the high part has them set.
//   0xfff8000000000000 - 0xffffffffffffffff
//   0x0000000000000000 - 0x0007ffffffffffff

#define VMA_BITS 52
#define HIGH_BITS (64 - VMA_BITS)

// The idea is to chop the high bits before doing the scaling, so the two
// parts become contiguous again and the usual scheme can be applied.

#define MEM_TO_SHADOW(mem)                                       \
  ((((mem) << HIGH_BITS) >> (HIGH_BITS + (ASAN_SHADOW_SCALE))) + \
   (ASAN_SHADOW_OFFSET))
#define SHADOW_TO_MEM(ptr) (__asan::ShadowToMemSparc64(ptr))

#define kLowMemBeg 0
#define kLowMemEnd (ASAN_SHADOW_OFFSET - 1)

#define kLowShadowBeg ASAN_SHADOW_OFFSET
#define kLowShadowEnd MEM_TO_SHADOW(kLowMemEnd)

// But of course there is the huge hole between the high shadow memory,
// which is in the low part, and the beginning of the high part.

#define kHighMemBeg (-(1ULL << (VMA_BITS - 1)))

#define kHighShadowBeg MEM_TO_SHADOW(kHighMemBeg)
#define kHighShadowEnd MEM_TO_SHADOW(kHighMemEnd)

#define kMidShadowBeg 0
#define kMidShadowEnd 0

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
#define kZeroBaseShadowStart 0
#define kZeroBaseMaxShadowStart (1 << 18)

#define kShadowGapBeg (kLowShadowEnd + 1)
#define kShadowGapEnd (kHighShadowBeg - 1)

#define kShadowGap2Beg 0
#define kShadowGap2End 0

#define kShadowGap3Beg 0
#define kShadowGap3End 0

namespace __asan {

static inline bool AddrIsInLowMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a <= kLowMemEnd;
}

static inline bool AddrIsInLowShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a >= kLowShadowBeg && a <= kLowShadowEnd;
}

static inline bool AddrIsInMidMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInMidShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInHighMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kHighMemBeg && a >= kHighMemBeg && a <= kHighMemEnd;
}

static inline bool AddrIsInHighShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kHighMemBeg && a >= kHighShadowBeg && a <= kHighShadowEnd;
}

static inline bool AddrIsInShadowGap(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a >= kShadowGapBeg && a <= kShadowGapEnd;
}

static inline constexpr uptr ShadowToMemSparc64(uptr p) {
  PROFILE_ASAN_MAPPING();
  p -= ASAN_SHADOW_OFFSET;
  p <<= ASAN_SHADOW_SCALE;
  if (p >= 0x8000000000000) {
    p |= (~0ULL) << VMA_BITS;
  }
  return p;
}

static_assert(ShadowToMemSparc64(MEM_TO_SHADOW(0x0000000000000000)) ==
              0x0000000000000000);
static_assert(ShadowToMemSparc64(MEM_TO_SHADOW(0xfff8000000000000)) ==
              0xfff8000000000000);
// Gets aligned down.
static_assert(ShadowToMemSparc64(MEM_TO_SHADOW(0x0007ffffffffffff)) ==
              0x0007fffffffffff8);

}  // namespace __asan

#endif  // ASAN_MAPPING_SPARC64_H
