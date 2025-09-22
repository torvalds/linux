//===-- sanitizer_atomic_msvc.h ---------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ATOMIC_MSVC_H
#define SANITIZER_ATOMIC_MSVC_H

extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)
extern "C" void _mm_mfence();
#pragma intrinsic(_mm_mfence)
extern "C" void _mm_pause();
#pragma intrinsic(_mm_pause)
extern "C" char _InterlockedExchange8(char volatile *Addend, char Value);
#pragma intrinsic(_InterlockedExchange8)
extern "C" short _InterlockedExchange16(short volatile *Addend, short Value);
#pragma intrinsic(_InterlockedExchange16)
extern "C" long _InterlockedExchange(long volatile *Addend, long Value);
#pragma intrinsic(_InterlockedExchange)
extern "C" long _InterlockedExchangeAdd(long volatile *Addend, long Value);
#pragma intrinsic(_InterlockedExchangeAdd)
extern "C" char _InterlockedCompareExchange8(char volatile *Destination,
                                             char Exchange, char Comparand);
#pragma intrinsic(_InterlockedCompareExchange8)
extern "C" short _InterlockedCompareExchange16(short volatile *Destination,
                                               short Exchange, short Comparand);
#pragma intrinsic(_InterlockedCompareExchange16)
extern "C" long long _InterlockedCompareExchange64(
    long long volatile *Destination, long long Exchange, long long Comparand);
#pragma intrinsic(_InterlockedCompareExchange64)
extern "C" void *_InterlockedCompareExchangePointer(
    void *volatile *Destination,
    void *Exchange, void *Comparand);
#pragma intrinsic(_InterlockedCompareExchangePointer)
extern "C" long __cdecl _InterlockedCompareExchange(long volatile *Destination,
                                                    long Exchange,
                                                    long Comparand);
#pragma intrinsic(_InterlockedCompareExchange)

#ifdef _WIN64
extern "C" long long _InterlockedExchangeAdd64(long long volatile *Addend,
                                               long long Value);
#pragma intrinsic(_InterlockedExchangeAdd64)
#endif

namespace __sanitizer {

inline void atomic_signal_fence(memory_order) {
  _ReadWriteBarrier();
}

inline void atomic_thread_fence(memory_order) {
  _mm_mfence();
}

inline void proc_yield(int cnt) {
  for (int i = 0; i < cnt; i++)
    _mm_pause();
}

template<typename T>
inline typename T::Type atomic_load(
    const volatile T *a, memory_order mo) {
  DCHECK(mo == memory_order_relaxed || mo == memory_order_consume ||
         mo == memory_order_acquire || mo == memory_order_seq_cst);
  DCHECK(!((uptr)a % sizeof(*a)));
  typename T::Type v;
  // FIXME(dvyukov): 64-bit load is not atomic on 32-bits.
  if (mo == memory_order_relaxed) {
    v = a->val_dont_use;
  } else {
    atomic_signal_fence(memory_order_seq_cst);
    v = a->val_dont_use;
    atomic_signal_fence(memory_order_seq_cst);
  }
  return v;
}

template<typename T>
inline void atomic_store(volatile T *a, typename T::Type v, memory_order mo) {
  DCHECK(mo == memory_order_relaxed || mo == memory_order_release ||
         mo == memory_order_seq_cst);
  DCHECK(!((uptr)a % sizeof(*a)));
  // FIXME(dvyukov): 64-bit store is not atomic on 32-bits.
  if (mo == memory_order_relaxed) {
    a->val_dont_use = v;
  } else {
    atomic_signal_fence(memory_order_seq_cst);
    a->val_dont_use = v;
    atomic_signal_fence(memory_order_seq_cst);
  }
  if (mo == memory_order_seq_cst)
    atomic_thread_fence(memory_order_seq_cst);
}

inline u32 atomic_fetch_add(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchangeAdd((volatile long *)&a->val_dont_use,
                                      (long)v);
}

inline uptr atomic_fetch_add(volatile atomic_uintptr_t *a,
    uptr v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
#ifdef _WIN64
  return (uptr)_InterlockedExchangeAdd64((volatile long long *)&a->val_dont_use,
                                         (long long)v);
#else
  return (uptr)_InterlockedExchangeAdd((volatile long *)&a->val_dont_use,
                                       (long)v);
#endif
}

inline u32 atomic_fetch_sub(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchangeAdd((volatile long *)&a->val_dont_use,
                                      -(long)v);
}

inline uptr atomic_fetch_sub(volatile atomic_uintptr_t *a,
    uptr v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
#ifdef _WIN64
  return (uptr)_InterlockedExchangeAdd64((volatile long long *)&a->val_dont_use,
                                         -(long long)v);
#else
  return (uptr)_InterlockedExchangeAdd((volatile long *)&a->val_dont_use,
                                       -(long)v);
#endif
}

inline u8 atomic_exchange(volatile atomic_uint8_t *a,
    u8 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u8)_InterlockedExchange8((volatile char*)&a->val_dont_use, v);
}

inline u16 atomic_exchange(volatile atomic_uint16_t *a,
    u16 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u16)_InterlockedExchange16((volatile short*)&a->val_dont_use, v);
}

inline u32 atomic_exchange(volatile atomic_uint32_t *a,
    u32 v, memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  return (u32)_InterlockedExchange((volatile long*)&a->val_dont_use, v);
}

inline bool atomic_compare_exchange_strong(volatile atomic_uint8_t *a,
                                           u8 *cmp,
                                           u8 xchgv,
                                           memory_order mo) {
  (void)mo;
  DCHECK(!((uptr)a % sizeof(*a)));
  u8 cmpv = *cmp;
#ifdef _WIN64
  u8 prev = (u8)_InterlockedCompareExchange8(
      (volatile char*)&a->val_dont_use, (char)xchgv, (char)cmpv);
#else
  u8 prev;
  __asm {
    mov al, cmpv
    mov ecx, a
    mov dl, xchgv
    lock cmpxchg [ecx], dl
    mov prev, al
  }
#endif
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

inline bool atomic_compare_exchange_strong(volatile atomic_uintptr_t *a,
                                           uptr *cmp,
                                           uptr xchg,
                                           memory_order mo) {
  uptr cmpv = *cmp;
  uptr prev = (uptr)_InterlockedCompareExchangePointer(
      (void*volatile*)&a->val_dont_use, (void*)xchg, (void*)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

inline bool atomic_compare_exchange_strong(volatile atomic_uint16_t *a,
                                           u16 *cmp,
                                           u16 xchg,
                                           memory_order mo) {
  u16 cmpv = *cmp;
  u16 prev = (u16)_InterlockedCompareExchange16(
      (volatile short*)&a->val_dont_use, (short)xchg, (short)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

inline bool atomic_compare_exchange_strong(volatile atomic_uint32_t *a,
                                           u32 *cmp,
                                           u32 xchg,
                                           memory_order mo) {
  u32 cmpv = *cmp;
  u32 prev = (u32)_InterlockedCompareExchange(
      (volatile long*)&a->val_dont_use, (long)xchg, (long)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

inline bool atomic_compare_exchange_strong(volatile atomic_uint64_t *a,
                                           u64 *cmp,
                                           u64 xchg,
                                           memory_order mo) {
  u64 cmpv = *cmp;
  u64 prev = (u64)_InterlockedCompareExchange64(
      (volatile long long*)&a->val_dont_use, (long long)xchg, (long long)cmpv);
  if (prev == cmpv)
    return true;
  *cmp = prev;
  return false;
}

template<typename T>
inline bool atomic_compare_exchange_weak(volatile T *a,
                                         typename T::Type *cmp,
                                         typename T::Type xchg,
                                         memory_order mo) {
  return atomic_compare_exchange_strong(a, cmp, xchg, mo);
}

}  // namespace __sanitizer

#endif  // SANITIZER_ATOMIC_CLANG_H
