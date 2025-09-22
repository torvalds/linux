//===-- asan_premap_shadow.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Reserve shadow memory with an ifunc resolver.
//===----------------------------------------------------------------------===//

#include "asan_mapping.h"

#if ASAN_PREMAP_SHADOW

#include "asan_premap_shadow.h"
#include "sanitizer_common/sanitizer_posix.h"

namespace __asan {

// The code in this file needs to run in an unrelocated binary. It may not
// access any external symbol, including its own non-hidden globals.

// Conservative upper limit.
uptr PremapShadowSize() {
  uptr granularity = GetMmapGranularity();
  return RoundUpTo(GetMaxVirtualAddress() >> ASAN_SHADOW_SCALE, granularity);
}

// Returns an address aligned to 8 pages, such that one page on the left and
// PremapShadowSize() bytes on the right of it are mapped r/o.
uptr PremapShadow() {
  return MapDynamicShadow(PremapShadowSize(), /*mmap_alignment_scale*/ 3,
                          /*min_shadow_base_alignment*/ 0, kHighMemEnd,
                          GetMmapGranularity());
}

bool PremapShadowFailed() {
  uptr shadow = reinterpret_cast<uptr>(&__asan_shadow);
  uptr resolver = reinterpret_cast<uptr>(&__asan_premap_shadow);
  // shadow == resolver is how Android KitKat and older handles ifunc.
  // shadow == 0 just in case.
  if (shadow == 0 || shadow == resolver)
    return true;
  return false;
}
} // namespace __asan

extern "C" {
decltype(__asan_shadow)* __asan_premap_shadow() {
  // The resolver may be called multiple times. Map the shadow just once.
  static uptr premapped_shadow = 0;
  if (!premapped_shadow) premapped_shadow = __asan::PremapShadow();
  return reinterpret_cast<decltype(__asan_shadow)*>(premapped_shadow);
}

// __asan_shadow is a "function" that has the same address as the first byte of
// the shadow mapping.
INTERFACE_ATTRIBUTE __attribute__((ifunc("__asan_premap_shadow"))) void
__asan_shadow();
}

#endif // ASAN_PREMAP_SHADOW
