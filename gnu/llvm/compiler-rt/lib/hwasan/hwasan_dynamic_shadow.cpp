//===-- hwasan_dynamic_shadow.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer. It reserves dynamic shadow memory
/// region and handles ifunc resolver case, when necessary.
///
//===----------------------------------------------------------------------===//

#include "hwasan_dynamic_shadow.h"

#include <elf.h>
#include <link.h>

#include "hwasan.h"
#include "hwasan_mapping.h"
#include "hwasan_thread_list.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_posix.h"

// The code in this file needs to run in an unrelocated binary. It should not
// access any external symbol, including its own non-hidden globals.

#if SANITIZER_ANDROID
extern "C" {

INTERFACE_ATTRIBUTE void __hwasan_shadow();
decltype(__hwasan_shadow)* __hwasan_premap_shadow();

}  // extern "C"

namespace __hwasan {

// We cannot call anything in libc here (see comment above), so we need to
// assume the biggest allowed page size.
// Android max page size is defined as 16k here:
// https://android.googlesource.com/platform/bionic/+/main/libc/platform/bionic/page.h#41
static constexpr uptr kMaxGranularity = 16384;

// Conservative upper limit.
static uptr PremapShadowSize() {
  return RoundUpTo(GetMaxVirtualAddress() >> kShadowScale, kMaxGranularity);
}

static uptr PremapShadow() {
  return MapDynamicShadow(PremapShadowSize(), kShadowScale,
                          kShadowBaseAlignment, kHighMemEnd, kMaxGranularity);
}

static bool IsPremapShadowAvailable() {
  const uptr shadow = reinterpret_cast<uptr>(&__hwasan_shadow);
  const uptr resolver = reinterpret_cast<uptr>(&__hwasan_premap_shadow);
  // shadow == resolver is how Android KitKat and older handles ifunc.
  // shadow == 0 just in case.
  return shadow != 0 && shadow != resolver;
}

static uptr FindPremappedShadowStart(uptr shadow_size_bytes) {
  const uptr granularity = kMaxGranularity;
  const uptr shadow_start = reinterpret_cast<uptr>(&__hwasan_shadow);
  const uptr premap_shadow_size = PremapShadowSize();
  const uptr shadow_size = RoundUpTo(shadow_size_bytes, granularity);

  // We may have mapped too much. Release extra memory.
  UnmapFromTo(shadow_start + shadow_size, shadow_start + premap_shadow_size);
  return shadow_start;
}

}  // namespace __hwasan

extern "C" {

decltype(__hwasan_shadow)* __hwasan_premap_shadow() {
  // The resolver might be called multiple times. Map the shadow just once.
  static __sanitizer::uptr shadow = 0;
  if (!shadow)
    shadow = __hwasan::PremapShadow();
  return reinterpret_cast<decltype(__hwasan_shadow)*>(shadow);
}

// __hwasan_shadow is a "function" that has the same address as the first byte
// of the shadow mapping.
INTERFACE_ATTRIBUTE __attribute__((ifunc("__hwasan_premap_shadow")))
void __hwasan_shadow();

extern __attribute((weak, visibility("hidden"))) ElfW(Rela) __rela_iplt_start[],
    __rela_iplt_end[];

}  // extern "C"

namespace __hwasan {

void InitShadowGOT() {
  // Call the ifunc resolver for __hwasan_shadow and fill in its GOT entry. This
  // needs to be done before other ifunc resolvers (which are handled by libc)
  // because a resolver might read __hwasan_shadow.
  typedef ElfW(Addr) (*ifunc_resolver_t)(void);
  for (ElfW(Rela) *r = __rela_iplt_start; r != __rela_iplt_end; ++r) {
    ElfW(Addr)* offset = reinterpret_cast<ElfW(Addr)*>(r->r_offset);
    ElfW(Addr) resolver = r->r_addend;
    if (resolver == reinterpret_cast<ElfW(Addr)>(&__hwasan_premap_shadow)) {
      *offset = reinterpret_cast<ifunc_resolver_t>(resolver)();
      break;
    }
  }
}

uptr FindDynamicShadowStart(uptr shadow_size_bytes) {
  if (IsPremapShadowAvailable())
    return FindPremappedShadowStart(shadow_size_bytes);
  return MapDynamicShadow(shadow_size_bytes, kShadowScale, kShadowBaseAlignment,
                          kHighMemEnd, kMaxGranularity);
}

}  // namespace __hwasan

#elif SANITIZER_FUCHSIA

namespace __hwasan {

void InitShadowGOT() {}

}  // namespace __hwasan

#else
namespace __hwasan {

void InitShadowGOT() {}

uptr FindDynamicShadowStart(uptr shadow_size_bytes) {
#  if defined(HWASAN_ALIASING_MODE)
  constexpr uptr kAliasSize = 1ULL << kAddressTagShift;
  constexpr uptr kNumAliases = 1ULL << kTagBits;
  return MapDynamicShadowAndAliases(shadow_size_bytes, kAliasSize, kNumAliases,
                                    RingBufferSize());
#  endif
  return MapDynamicShadow(shadow_size_bytes, kShadowScale, kShadowBaseAlignment,
                          kHighMemEnd, GetMmapGranularity());
}

}  // namespace __hwasan

#endif  // SANITIZER_ANDROID
