//===-- sanitizer_lfstack.h -=-----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lock-free stack.
// Uses 32/17 bits as ABA-counter on 32/64-bit platforms.
// The memory passed to Push() must not be ever munmap'ed.
// The type T must contain T *next field.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LFSTACK_H
#define SANITIZER_LFSTACK_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_common.h"
#include "sanitizer_atomic.h"

namespace __sanitizer {

template<typename T>
struct LFStack {
  void Clear() {
    atomic_store(&head_, 0, memory_order_relaxed);
  }

  bool Empty() const {
    return (atomic_load(&head_, memory_order_relaxed) & kPtrMask) == 0;
  }

  void Push(T *p) {
    u64 cmp = atomic_load(&head_, memory_order_relaxed);
    for (;;) {
      u64 cnt = (cmp & kCounterMask) + kCounterInc;
      u64 xch = (u64)(uptr)p | cnt;
      p->next = (T*)(uptr)(cmp & kPtrMask);
      if (atomic_compare_exchange_weak(&head_, &cmp, xch,
                                       memory_order_release))
        break;
    }
  }

  T *Pop() {
    u64 cmp = atomic_load(&head_, memory_order_acquire);
    for (;;) {
      T *cur = (T*)(uptr)(cmp & kPtrMask);
      if (!cur)
        return nullptr;
      T *nxt = cur->next;
      u64 cnt = (cmp & kCounterMask);
      u64 xch = (u64)(uptr)nxt | cnt;
      if (atomic_compare_exchange_weak(&head_, &cmp, xch,
                                       memory_order_acquire))
        return cur;
    }
  }

  // private:
  static const int kCounterBits = FIRST_32_SECOND_64(32, 17);
  static const u64 kPtrMask = ((u64)-1) >> kCounterBits;
  static const u64 kCounterMask = ~kPtrMask;
  static const u64 kCounterInc = kPtrMask + 1;

  atomic_uint64_t head_;
};
} // namespace __sanitizer

#endif // SANITIZER_LFSTACK_H
