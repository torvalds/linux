//===-- hwasan_malloc_bisect.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_hash.h"
#include "hwasan.h"

namespace __hwasan {

static u32 malloc_hash(StackTrace *stack, uptr orig_size) {
  uptr len = Min(stack->size, (unsigned)7);
  MurMur2HashBuilder H(len);
  H.add(orig_size);
  // Start with frame #1 to skip __sanitizer_malloc frame, which is
  // (a) almost always the same (well, could be operator new or new[])
  // (b) can change hashes when compiler-rt is rebuilt, invalidating previous
  // bisection results.
  // Because of ASLR, use only offset inside the page.
  for (uptr i = 1; i < len; ++i) H.add(((u32)stack->trace[i]) & 0xFFF);
  return H.get();
}

static inline bool malloc_bisect(StackTrace *stack, uptr orig_size) {
  uptr left = flags()->malloc_bisect_left;
  uptr right = flags()->malloc_bisect_right;
  if (LIKELY(left == 0 && right == 0))
    return true;
  if (!stack)
    return true;
  // Allow malloc_bisect_right > (u32)(-1) to avoid spelling the latter in
  // decimal.
  uptr h = (uptr)malloc_hash(stack, orig_size);
  if (h < left || h > right)
    return false;
  if (flags()->malloc_bisect_dump) {
    Printf("[alloc] %u %zu\n", h, orig_size);
    stack->Print();
  }
  return true;
}

}  // namespace __hwasan
