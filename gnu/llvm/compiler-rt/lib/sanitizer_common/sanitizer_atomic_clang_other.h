//===-- sanitizer_atomic_clang_other.h --------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ATOMIC_CLANG_OTHER_H
#define SANITIZER_ATOMIC_CLANG_OTHER_H

namespace __sanitizer {


inline void proc_yield(int cnt) {
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
      __sync_synchronize();
    } else {  // seq_cst
      // E.g. on POWER we need a hw fence even before the store.
      __sync_synchronize();
      v = a->val_dont_use;
      __sync_synchronize();
    }
  } else {
    __atomic_load(const_cast<typename T::Type volatile *>(&a->val_dont_use), &v,
                  __ATOMIC_SEQ_CST);
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
      __sync_synchronize();
      a->val_dont_use = v;
      __asm__ __volatile__("" ::: "memory");
    } else {  // seq_cst
      __sync_synchronize();
      a->val_dont_use = v;
      __sync_synchronize();
    }
  } else {
    __atomic_store(&a->val_dont_use, &v, __ATOMIC_SEQ_CST);
  }
}

}  // namespace __sanitizer

#endif  // #ifndef SANITIZER_ATOMIC_CLANG_OTHER_H
