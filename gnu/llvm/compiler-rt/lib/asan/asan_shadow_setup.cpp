//===-- asan_shadow_setup.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Set up the shadow memory.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

// asan_fuchsia.cpp has their own InitializeShadowMemory implementation.
#if !SANITIZER_FUCHSIA

#  include "asan_internal.h"
#  include "asan_mapping.h"

namespace __asan {

static void ProtectGap(uptr addr, uptr size) {
  if (!flags()->protect_shadow_gap) {
    // The shadow gap is unprotected, so there is a chance that someone
    // is actually using this memory. Which means it needs a shadow...
    uptr GapShadowBeg = RoundDownTo(MEM_TO_SHADOW(addr), GetPageSizeCached());
    uptr GapShadowEnd =
        RoundUpTo(MEM_TO_SHADOW(addr + size), GetPageSizeCached()) - 1;
    if (Verbosity())
      Printf(
          "protect_shadow_gap=0:"
          " not protecting shadow gap, allocating gap's shadow\n"
          "|| `[%p, %p]` || ShadowGap's shadow ||\n",
          (void*)GapShadowBeg, (void*)GapShadowEnd);
    ReserveShadowMemoryRange(GapShadowBeg, GapShadowEnd,
                             "unprotected gap shadow");
    return;
  }
  __sanitizer::ProtectGap(addr, size, kZeroBaseShadowStart,
                          kZeroBaseMaxShadowStart);
}

static void MaybeReportLinuxPIEBug() {
#if SANITIZER_LINUX && \
    (defined(__x86_64__) || defined(__aarch64__) || SANITIZER_RISCV64)
  Report("This might be related to ELF_ET_DYN_BASE change in Linux 4.12.\n");
  Report(
      "See https://github.com/google/sanitizers/issues/856 for possible "
      "workarounds.\n");
#endif
}

void InitializeShadowMemory() {
  // Set the shadow memory address to uninitialized.
  __asan_shadow_memory_dynamic_address = kDefaultShadowSentinel;

  uptr shadow_start = kLowShadowBeg;
  // Detect if a dynamic shadow address must used and find a available location
  // when necessary. When dynamic address is used, the macro |kLowShadowBeg|
  // expands to |__asan_shadow_memory_dynamic_address| which is
  // |kDefaultShadowSentinel|.
  bool full_shadow_is_available = false;
  if (shadow_start == kDefaultShadowSentinel) {
    shadow_start = FindDynamicShadowStart();
    if (SANITIZER_LINUX) full_shadow_is_available = true;
  }
  // Update the shadow memory address (potentially) used by instrumentation.
  __asan_shadow_memory_dynamic_address = shadow_start;

  if (kLowShadowBeg) shadow_start -= GetMmapGranularity();

  if (!full_shadow_is_available)
    full_shadow_is_available =
        MemoryRangeIsAvailable(shadow_start, kHighShadowEnd);

#if SANITIZER_LINUX && defined(__x86_64__) && defined(_LP64) && \
    !ASAN_FIXED_MAPPING
  if (!full_shadow_is_available) {
    kMidMemBeg = kLowMemEnd < 0x3000000000ULL ? 0x3000000000ULL : 0;
    kMidMemEnd = kLowMemEnd < 0x3000000000ULL ? 0x4fffffffffULL : 0;
  }
#endif

  if (Verbosity()) PrintAddressSpaceLayout();

  if (full_shadow_is_available) {
    // mmap the low shadow plus at least one page at the left.
    if (kLowShadowBeg)
      ReserveShadowMemoryRange(shadow_start, kLowShadowEnd, "low shadow");
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd, "high shadow");
    // protect the gap.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
    CHECK_EQ(kShadowGapEnd, kHighShadowBeg - 1);
  } else if (kMidMemBeg &&
             MemoryRangeIsAvailable(shadow_start, kMidMemBeg - 1) &&
             MemoryRangeIsAvailable(kMidMemEnd + 1, kHighShadowEnd)) {
    CHECK(kLowShadowBeg != kLowShadowEnd);
    // mmap the low shadow plus at least one page at the left.
    ReserveShadowMemoryRange(shadow_start, kLowShadowEnd, "low shadow");
    // mmap the mid shadow.
    ReserveShadowMemoryRange(kMidShadowBeg, kMidShadowEnd, "mid shadow");
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd, "high shadow");
    // protect the gaps.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
    ProtectGap(kShadowGap2Beg, kShadowGap2End - kShadowGap2Beg + 1);
    ProtectGap(kShadowGap3Beg, kShadowGap3End - kShadowGap3Beg + 1);
  } else {
    Report(
        "Shadow memory range interleaves with an existing memory mapping. "
        "ASan cannot proceed correctly. ABORTING.\n");
    Report("ASan shadow was supposed to be located in the [%p-%p] range.\n",
           (void*)shadow_start, (void*)kHighShadowEnd);
    MaybeReportLinuxPIEBug();
    DumpProcessMap();
    Die();
  }
}

}  // namespace __asan

#endif  // !SANITIZER_FUCHSIA
