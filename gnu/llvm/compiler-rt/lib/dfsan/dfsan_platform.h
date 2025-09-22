//===-- dfsan_platform.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Platform specific information for DFSan.
//===----------------------------------------------------------------------===//

#ifndef DFSAN_PLATFORM_H
#define DFSAN_PLATFORM_H

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_platform.h"

namespace __dfsan {

using __sanitizer::uptr;

// TODO: The memory mapping code to setup a 1:1 shadow is based on msan.
// Consider refactoring these into a shared implementation.

struct MappingDesc {
  uptr start;
  uptr end;
  enum Type {
    INVALID = 1,
    ALLOCATOR = 2,
    APP = 4,
    SHADOW = 8,
    ORIGIN = 16,
  } type;
  const char *name;
};

// Note: MappingDesc::ALLOCATOR entries are only used to check for memory
// layout compatibility. The actual allocation settings are in
// dfsan_allocator.cpp, which need to be kept in sync.
#if SANITIZER_LINUX && SANITIZER_WORDSIZE == 64

#  if defined(__aarch64__)
// The mapping assumes 48-bit VMA. AArch64 maps:
// - 0x0000000000000-0x0100000000000: 39/42/48-bits program own segments
// - 0x0a00000000000-0x0b00000000000: 48-bits PIE program segments
//   Ideally, this would extend to 0x0c00000000000 (2^45 bytes - the
//   maximum ASLR region for 48-bit VMA) but it is too hard to fit in
//   the larger app/shadow/origin regions.
// - 0x0e00000000000-0x1000000000000: 48-bits libraries segments
const MappingDesc kMemoryLayout[] = {
    {0X0000000000000, 0X0100000000000, MappingDesc::APP, "app-10-13"},
    {0X0100000000000, 0X0200000000000, MappingDesc::SHADOW, "shadow-14"},
    {0X0200000000000, 0X0300000000000, MappingDesc::INVALID, "invalid"},
    {0X0300000000000, 0X0400000000000, MappingDesc::ORIGIN, "origin-14"},
    {0X0400000000000, 0X0600000000000, MappingDesc::SHADOW, "shadow-15"},
    {0X0600000000000, 0X0800000000000, MappingDesc::ORIGIN, "origin-15"},
    {0X0800000000000, 0X0A00000000000, MappingDesc::INVALID, "invalid"},
    {0X0A00000000000, 0X0B00000000000, MappingDesc::APP, "app-14"},
    {0X0B00000000000, 0X0C00000000000, MappingDesc::SHADOW, "shadow-10-13"},
    {0X0C00000000000, 0X0D00000000000, MappingDesc::INVALID, "invalid"},
    {0X0D00000000000, 0X0E00000000000, MappingDesc::ORIGIN, "origin-10-13"},
    {0X0E00000000000, 0X0E40000000000, MappingDesc::ALLOCATOR, "allocator"},
    {0X0E40000000000, 0X1000000000000, MappingDesc::APP, "app-15"},
};
#    define MEM_TO_SHADOW(mem) ((uptr)mem ^ 0xB00000000000ULL)
#    define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x200000000000ULL)

#  else
// All of the following configurations are supported.
// ASLR disabled: main executable and DSOs at 0x555550000000
// PIE and ASLR: main executable and DSOs at 0x7f0000000000
// non-PIE: main executable below 0x100000000, DSOs at 0x7f0000000000
// Heap at 0x700000000000.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "app-1"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x100000000000ULL, 0x110000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x110000000000ULL, 0x200000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x200000000000ULL, 0x300000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x300000000000ULL, 0x400000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x400000000000ULL, 0x500000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x500000000000ULL, 0x510000000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x510000000000ULL, 0x600000000000ULL, MappingDesc::APP, "app-2"},
    {0x600000000000ULL, 0x610000000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x610000000000ULL, 0x700000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x700000000000ULL, 0x740000000000ULL, MappingDesc::ALLOCATOR, "allocator"},
    {0x740000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app-3"}};
#    define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x500000000000ULL)
#    define SHADOW_TO_ORIGIN(mem) (((uptr)(mem)) + 0x100000000000ULL)
#  endif

#else
#  error "Unsupported platform"
#endif

const uptr kMemoryLayoutSize = sizeof(kMemoryLayout) / sizeof(kMemoryLayout[0]);

#define MEM_TO_ORIGIN(mem) (SHADOW_TO_ORIGIN(MEM_TO_SHADOW((mem))))

#ifndef __clang__
__attribute__((optimize("unroll-loops")))
#endif
inline bool
addr_is_type(uptr addr, int mapping_types) {
// It is critical for performance that this loop is unrolled (because then it is
// simplified into just a few constant comparisons).
#ifdef __clang__
#  pragma unroll
#endif
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i)
    if ((kMemoryLayout[i].type & mapping_types) &&
        addr >= kMemoryLayout[i].start && addr < kMemoryLayout[i].end)
      return true;
  return false;
}

#define MEM_IS_APP(mem) \
  (addr_is_type((uptr)(mem), MappingDesc::APP | MappingDesc::ALLOCATOR))
#define MEM_IS_SHADOW(mem) addr_is_type((uptr)(mem), MappingDesc::SHADOW)
#define MEM_IS_ORIGIN(mem) addr_is_type((uptr)(mem), MappingDesc::ORIGIN)

}  // namespace __dfsan

#endif
