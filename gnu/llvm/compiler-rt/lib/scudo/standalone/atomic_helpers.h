//===-- atomic_helpers.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ATOMIC_H_
#define SCUDO_ATOMIC_H_

#include "internal_defs.h"

namespace scudo {

enum memory_order {
  memory_order_relaxed = 0,
  memory_order_consume = 1,
  memory_order_acquire = 2,
  memory_order_release = 3,
  memory_order_acq_rel = 4,
  memory_order_seq_cst = 5
};
static_assert(memory_order_relaxed == __ATOMIC_RELAXED, "");
static_assert(memory_order_consume == __ATOMIC_CONSUME, "");
static_assert(memory_order_acquire == __ATOMIC_ACQUIRE, "");
static_assert(memory_order_release == __ATOMIC_RELEASE, "");
static_assert(memory_order_acq_rel == __ATOMIC_ACQ_REL, "");
static_assert(memory_order_seq_cst == __ATOMIC_SEQ_CST, "");

struct atomic_u8 {
  typedef u8 Type;
  volatile Type ValDoNotUse;
};

struct atomic_u16 {
  typedef u16 Type;
  volatile Type ValDoNotUse;
};

struct atomic_s32 {
  typedef s32 Type;
  volatile Type ValDoNotUse;
};

struct atomic_u32 {
  typedef u32 Type;
  volatile Type ValDoNotUse;
};

struct atomic_u64 {
  typedef u64 Type;
  // On 32-bit platforms u64 is not necessarily aligned on 8 bytes.
  alignas(8) volatile Type ValDoNotUse;
};

struct atomic_uptr {
  typedef uptr Type;
  volatile Type ValDoNotUse;
};

template <typename T>
inline typename T::Type atomic_load(const volatile T *A, memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  typename T::Type V;
  __atomic_load(&A->ValDoNotUse, &V, MO);
  return V;
}

template <typename T>
inline void atomic_store(volatile T *A, typename T::Type V, memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  __atomic_store(&A->ValDoNotUse, &V, MO);
}

inline void atomic_thread_fence(memory_order) { __sync_synchronize(); }

template <typename T>
inline typename T::Type atomic_fetch_add(volatile T *A, typename T::Type V,
                                         memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  return __atomic_fetch_add(&A->ValDoNotUse, V, MO);
}

template <typename T>
inline typename T::Type atomic_fetch_sub(volatile T *A, typename T::Type V,
                                         memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  return __atomic_fetch_sub(&A->ValDoNotUse, V, MO);
}

template <typename T>
inline typename T::Type atomic_fetch_and(volatile T *A, typename T::Type V,
                                         memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  return __atomic_fetch_and(&A->ValDoNotUse, V, MO);
}

template <typename T>
inline typename T::Type atomic_fetch_or(volatile T *A, typename T::Type V,
                                        memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  return __atomic_fetch_or(&A->ValDoNotUse, V, MO);
}

template <typename T>
inline typename T::Type atomic_exchange(volatile T *A, typename T::Type V,
                                        memory_order MO) {
  DCHECK(!(reinterpret_cast<uptr>(A) % sizeof(*A)));
  typename T::Type R;
  __atomic_exchange(&A->ValDoNotUse, &V, &R, MO);
  return R;
}

template <typename T>
inline bool atomic_compare_exchange_strong(volatile T *A, typename T::Type *Cmp,
                                           typename T::Type Xchg,
                                           memory_order MO) {
  return __atomic_compare_exchange(&A->ValDoNotUse, Cmp, &Xchg, false, MO,
                                   __ATOMIC_RELAXED);
}

// Clutter-reducing helpers.

template <typename T>
inline typename T::Type atomic_load_relaxed(const volatile T *A) {
  return atomic_load(A, memory_order_relaxed);
}

template <typename T>
inline void atomic_store_relaxed(volatile T *A, typename T::Type V) {
  atomic_store(A, V, memory_order_relaxed);
}

template <typename T>
inline typename T::Type
atomic_compare_exchange_strong(volatile T *A, typename T::Type Cmp,
                               typename T::Type Xchg, memory_order MO) {
  atomic_compare_exchange_strong(A, &Cmp, Xchg, MO);
  return Cmp;
}

} // namespace scudo

#endif // SCUDO_ATOMIC_H_
