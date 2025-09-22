//===-- asan_interceptors_memintrinsics.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_interceptors_memintrinsics.cpp
//===---------------------------------------------------------------------===//
#ifndef ASAN_MEMINTRIN_H
#define ASAN_MEMINTRIN_H

#include "asan_interface_internal.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "interception/interception.h"

DECLARE_REAL(void *, memcpy, void *to, const void *from, uptr size)
DECLARE_REAL(void *, memset, void *block, int c, uptr size)

namespace __asan {

// Return true if we can quickly decide that the region is unpoisoned.
// We assume that a redzone is at least 16 bytes.
static inline bool QuickCheckForUnpoisonedRegion(uptr beg, uptr size) {
  if (UNLIKELY(size == 0 || size > sizeof(uptr) * ASAN_SHADOW_GRANULARITY))
    return !size;

  uptr last = beg + size - 1;
  uptr shadow_first = MEM_TO_SHADOW(beg);
  uptr shadow_last = MEM_TO_SHADOW(last);
  uptr uptr_first = RoundDownTo(shadow_first, sizeof(uptr));
  uptr uptr_last = RoundDownTo(shadow_last, sizeof(uptr));
  if (LIKELY(((*reinterpret_cast<const uptr *>(uptr_first) |
               *reinterpret_cast<const uptr *>(uptr_last)) == 0)))
    return true;
  u8 shadow = AddressIsPoisoned(last);
  for (; shadow_first < shadow_last; ++shadow_first)
    shadow |= *((u8 *)shadow_first);
  return !shadow;
}

struct AsanInterceptorContext {
  const char *interceptor_name;
};

// We implement ACCESS_MEMORY_RANGE, ASAN_READ_RANGE,
// and ASAN_WRITE_RANGE as macro instead of function so
// that no extra frames are created, and stack trace contains
// relevant information only.
// We check all shadow bytes.
#define ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite)                   \
  do {                                                                    \
    uptr __offset = (uptr)(offset);                                       \
    uptr __size = (uptr)(size);                                           \
    uptr __bad = 0;                                                       \
    if (UNLIKELY(__offset > __offset + __size)) {                         \
      GET_STACK_TRACE_FATAL_HERE;                                         \
      ReportStringFunctionSizeOverflow(__offset, __size, &stack);         \
    }                                                                     \
    if (UNLIKELY(!QuickCheckForUnpoisonedRegion(__offset, __size)) &&     \
        (__bad = __asan_region_is_poisoned(__offset, __size))) {          \
      AsanInterceptorContext *_ctx = (AsanInterceptorContext *)ctx;       \
      bool suppressed = false;                                            \
      if (_ctx) {                                                         \
        suppressed = IsInterceptorSuppressed(_ctx->interceptor_name);     \
        if (!suppressed && HaveStackTraceBasedSuppressions()) {           \
          GET_STACK_TRACE_FATAL_HERE;                                     \
          suppressed = IsStackTraceSuppressed(&stack);                    \
        }                                                                 \
      }                                                                   \
      if (!suppressed) {                                                  \
        GET_CURRENT_PC_BP_SP;                                             \
        ReportGenericError(pc, bp, sp, __bad, isWrite, __size, 0, false); \
      }                                                                   \
    }                                                                     \
  } while (0)

#define ASAN_READ_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, false)
#define ASAN_WRITE_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, true)

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool RangesOverlap(const char *offset1, uptr length1,
                                 const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}
#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)   \
  do {                                                                     \
    const char *offset1 = (const char *)_offset1;                          \
    const char *offset2 = (const char *)_offset2;                          \
    if (UNLIKELY(RangesOverlap(offset1, length1, offset2, length2))) {     \
      GET_STACK_TRACE_FATAL_HERE;                                          \
      bool suppressed = IsInterceptorSuppressed(name);                     \
      if (!suppressed && HaveStackTraceBasedSuppressions()) {              \
        suppressed = IsStackTraceSuppressed(&stack);                       \
      }                                                                    \
      if (!suppressed) {                                                   \
        ReportStringFunctionMemoryRangesOverlap(name, offset1, length1,    \
                                                offset2, length2, &stack); \
      }                                                                    \
    }                                                                      \
  } while (0)

}  // namespace __asan

#endif  // ASAN_MEMINTRIN_H
