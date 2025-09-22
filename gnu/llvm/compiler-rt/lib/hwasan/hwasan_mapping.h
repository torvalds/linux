//===-- hwasan_mapping.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer and defines memory mapping.
///
//===----------------------------------------------------------------------===//

#ifndef HWASAN_MAPPING_H
#define HWASAN_MAPPING_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "hwasan_interface_internal.h"

// Typical mapping on Linux/x86_64:
// with dynamic shadow mapped at [0x770d59f40000, 0x7f0d59f40000]:
// || [0x7f0d59f40000, 0x7fffffffffff] || HighMem    ||
// || [0x7efe2f934000, 0x7f0d59f3ffff] || HighShadow ||
// || [0x7e7e2f934000, 0x7efe2f933fff] || ShadowGap  ||
// || [0x770d59f40000, 0x7e7e2f933fff] || LowShadow  ||
// || [0x000000000000, 0x770d59f3ffff] || LowMem     ||

// Typical mapping on Android/AArch64
// with dynamic shadow mapped: [0x007477480000, 0x007c77480000]:
// || [0x007c77480000, 0x007fffffffff] || HighMem    ||
// || [0x007c3ebc8000, 0x007c7747ffff] || HighShadow ||
// || [0x007bbebc8000, 0x007c3ebc7fff] || ShadowGap  ||
// || [0x007477480000, 0x007bbebc7fff] || LowShadow  ||
// || [0x000000000000, 0x00747747ffff] || LowMem     ||

// Reasonable values are 4 (for 1/16th shadow) and 6 (for 1/64th).
constexpr uptr kShadowScale = 4;
constexpr uptr kShadowAlignment = 1ULL << kShadowScale;

namespace __hwasan {

extern uptr kLowMemStart;
extern uptr kLowMemEnd;
extern uptr kLowShadowEnd;
extern uptr kLowShadowStart;
extern uptr kHighShadowStart;
extern uptr kHighShadowEnd;
extern uptr kHighMemStart;
extern uptr kHighMemEnd;

inline uptr GetShadowOffset() {
  return SANITIZER_FUCHSIA ? 0 : __hwasan_shadow_memory_dynamic_address;
}
inline uptr MemToShadow(uptr untagged_addr) {
  return (untagged_addr >> kShadowScale) + GetShadowOffset();
}
inline uptr ShadowToMem(uptr shadow_addr) {
  return (shadow_addr - GetShadowOffset()) << kShadowScale;
}
inline uptr MemToShadowSize(uptr size) {
  return size >> kShadowScale;
}

bool MemIsApp(uptr p);

inline bool MemIsShadow(uptr p) {
  return (kLowShadowStart <= p && p <= kLowShadowEnd) ||
         (kHighShadowStart <= p && p <= kHighShadowEnd);
}

uptr GetAliasRegionStart();

}  // namespace __hwasan

#endif  // HWASAN_MAPPING_H
