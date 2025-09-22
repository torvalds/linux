//===-- memprof_shadow_setup.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Set up the shadow memory.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

#include "memprof_internal.h"
#include "memprof_mapping.h"

namespace __memprof {

static void ProtectGap(uptr addr, uptr size) {
  if (!flags()->protect_shadow_gap) {
    // The shadow gap is unprotected, so there is a chance that someone
    // is actually using this memory. Which means it needs a shadow...
    uptr GapShadowBeg = RoundDownTo(MEM_TO_SHADOW(addr), GetPageSizeCached());
    uptr GapShadowEnd =
        RoundUpTo(MEM_TO_SHADOW(addr + size), GetPageSizeCached()) - 1;
    if (Verbosity())
      Printf("protect_shadow_gap=0:"
             " not protecting shadow gap, allocating gap's shadow\n"
             "|| `[%p, %p]` || ShadowGap's shadow ||\n",
             GapShadowBeg, GapShadowEnd);
    ReserveShadowMemoryRange(GapShadowBeg, GapShadowEnd,
                             "unprotected gap shadow");
    return;
  }
  __sanitizer::ProtectGap(addr, size, kZeroBaseShadowStart,
                          kZeroBaseMaxShadowStart);
}

void InitializeShadowMemory() {
  uptr shadow_start = FindDynamicShadowStart();
  // Update the shadow memory address (potentially) used by instrumentation.
  __memprof_shadow_memory_dynamic_address = shadow_start;

  if (kLowShadowBeg)
    shadow_start -= GetMmapGranularity();

  if (Verbosity())
    PrintAddressSpaceLayout();

  // mmap the low shadow plus at least one page at the left.
  if (kLowShadowBeg)
    ReserveShadowMemoryRange(shadow_start, kLowShadowEnd, "low shadow");
  // mmap the high shadow.
  ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd, "high shadow");
  // protect the gap.
  ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
  CHECK_EQ(kShadowGapEnd, kHighShadowBeg - 1);
}

} // namespace __memprof
