//===-- sanitizer_atomic_clang_x86.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Not intended for direct inclusion. Include sanitizer_atomic.h.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ATOMIC_CLANG_X86_H
#define SANITIZER_ATOMIC_CLANG_X86_H

namespace __sanitizer {

inline void proc_yield(int cnt) {
  __asm__ __volatile__("" ::: "memory");
  for (int i = 0; i < cnt; i++)
    __asm__ __volatile__("pause");
  __asm__ __volatile__("" ::: "memory");
}

template<typename T>
inline typename T::Type atomic_load(
    const volatile T *a, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_consume
      | memory_order_acquire | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));
  typename T::Type v;

  if (sizeof(*a) < 8 || sizeof(void*) == 8) {
    // Assume that aligned loads are atomic.
    if (mo == memory_order_relaxed) {
      v = a->val_dont_use;
    } else if (mo == memory_order_consume) {
      // Assume that processor respects data dependencies
      // (and that compiler won't break them).
      __asm__ __volatile__("" ::: "memory");
      v = a->val_dont_use;
      __asm__ __volatile__("" ::: "memory");
    } else if (mo == memory_order_acquire) {
      __asm__ __volatile__("" ::: "memory");
      v = a->val_dont_use;
      // On x86 loads are implicitly acquire.
      __asm__ __volatile__("" ::: "memory");
    } else {  // seq_cst
      // On x86 plain MOV is enough for seq_cst store.
      __asm__ __volatile__("" ::: "memory");
      v = a->val_dont_use;
      __asm__ __volatile__("" ::: "memory");
    }
  } else {
    // 64-bit load on 32-bit platform.
    __asm__ __volatile__(
        "movq %1, %%mm0;"  // Use mmx reg for 64-bit atomic moves
        "movq %%mm0, %0;"  // (ptr could be read-only)
        "emms;"            // Empty mmx state/Reset FP regs
        : "=m" (v)
        : "m" (a->val_dont_use)
        : // mark the mmx registers as clobbered
#ifdef __MMX__
          "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
#endif  // #ifdef __MMX__
          "memory");
  }
  return v;
}

template<typename T>
inline void atomic_store(volatile T *a, typename T::Type v, memory_order mo) {
  DCHECK(mo & (memory_order_relaxed | memory_order_release
      | memory_order_seq_cst));
  DCHECK(!((uptr)a % sizeof(*a)));

  if (sizeof(*a) < 8 || sizeof(void*) == 8) {
    // Assume that aligned loads are atomic.
    if (mo == memory_order_relaxed) {
      a->val_dont_use = v;
    } else if (mo == memory_order_release) {
      // On x86 stores are implicitly release.
      __asm__ __volatile__("" ::: "memory");
      a->val_dont_use = v;
      __asm__ __volatile__("" ::: "memory");
    } else {  // seq_cst
      // On x86 stores are implicitly release.
      __asm__ __volatile__("" ::: "memory");
      a->val_dont_use = v;
      __sync_synchronize();
    }
  } else {
    // 64-bit store on 32-bit platform.
    __asm__ __volatile__(
        "movq %1, %%mm0;"  // Use mmx reg for 64-bit atomic moves
        "movq %%mm0, %0;"
        "emms;"            // Empty mmx state/Reset FP regs
        : "=m" (a->val_dont_use)
        : "m" (v)
        : // mark the mmx registers as clobbered
#ifdef __MMX__
          "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
#endif  // #ifdef __MMX__
          "memory");
    if (mo == memory_order_seq_cst)
      __sync_synchronize();
  }
}

}  // namespace __sanitizer

#endif  // #ifndef SANITIZER_ATOMIC_CLANG_X86_H
