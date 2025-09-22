//===-- tsan_interface_atomic.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

// ThreadSanitizer atomic operations are based on C++11/C1x standards.
// For background see C++11 standard.  A slightly older, publicly
// available draft of the standard (not entirely up-to-date, but close enough
// for casual browsing) is available here:
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3242.pdf
// The following page contains more background information:
// http://www.hpl.hp.com/personal/Hans_Boehm/c++mm/

#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "tsan_flags.h"
#include "tsan_interface.h"
#include "tsan_rtl.h"

using namespace __tsan;

#if !SANITIZER_GO && __TSAN_HAS_INT128
// Protects emulation of 128-bit atomic operations.
static StaticSpinMutex mutex128;
#endif

#if SANITIZER_DEBUG
static bool IsLoadOrder(morder mo) {
  return mo == mo_relaxed || mo == mo_consume
      || mo == mo_acquire || mo == mo_seq_cst;
}

static bool IsStoreOrder(morder mo) {
  return mo == mo_relaxed || mo == mo_release || mo == mo_seq_cst;
}
#endif

static bool IsReleaseOrder(morder mo) {
  return mo == mo_release || mo == mo_acq_rel || mo == mo_seq_cst;
}

static bool IsAcquireOrder(morder mo) {
  return mo == mo_consume || mo == mo_acquire
      || mo == mo_acq_rel || mo == mo_seq_cst;
}

static bool IsAcqRelOrder(morder mo) {
  return mo == mo_acq_rel || mo == mo_seq_cst;
}

template<typename T> T func_xchg(volatile T *v, T op) {
  T res = __sync_lock_test_and_set(v, op);
  // __sync_lock_test_and_set does not contain full barrier.
  __sync_synchronize();
  return res;
}

template<typename T> T func_add(volatile T *v, T op) {
  return __sync_fetch_and_add(v, op);
}

template<typename T> T func_sub(volatile T *v, T op) {
  return __sync_fetch_and_sub(v, op);
}

template<typename T> T func_and(volatile T *v, T op) {
  return __sync_fetch_and_and(v, op);
}

template<typename T> T func_or(volatile T *v, T op) {
  return __sync_fetch_and_or(v, op);
}

template<typename T> T func_xor(volatile T *v, T op) {
  return __sync_fetch_and_xor(v, op);
}

template<typename T> T func_nand(volatile T *v, T op) {
  // clang does not support __sync_fetch_and_nand.
  T cmp = *v;
  for (;;) {
    T newv = ~(cmp & op);
    T cur = __sync_val_compare_and_swap(v, cmp, newv);
    if (cmp == cur)
      return cmp;
    cmp = cur;
  }
}

template<typename T> T func_cas(volatile T *v, T cmp, T xch) {
  return __sync_val_compare_and_swap(v, cmp, xch);
}

// clang does not support 128-bit atomic ops.
// Atomic ops are executed under tsan internal mutex,
// here we assume that the atomic variables are not accessed
// from non-instrumented code.
#if !defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16) && !SANITIZER_GO \
    && __TSAN_HAS_INT128
a128 func_xchg(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = op;
  return cmp;
}

a128 func_add(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = cmp + op;
  return cmp;
}

a128 func_sub(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = cmp - op;
  return cmp;
}

a128 func_and(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = cmp & op;
  return cmp;
}

a128 func_or(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = cmp | op;
  return cmp;
}

a128 func_xor(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = cmp ^ op;
  return cmp;
}

a128 func_nand(volatile a128 *v, a128 op) {
  SpinMutexLock lock(&mutex128);
  a128 cmp = *v;
  *v = ~(cmp & op);
  return cmp;
}

a128 func_cas(volatile a128 *v, a128 cmp, a128 xch) {
  SpinMutexLock lock(&mutex128);
  a128 cur = *v;
  if (cur == cmp)
    *v = xch;
  return cur;
}
#endif

template <typename T>
static int AccessSize() {
  if (sizeof(T) <= 1)
    return 1;
  else if (sizeof(T) <= 2)
    return 2;
  else if (sizeof(T) <= 4)
    return 4;
  else
    return 8;
  // For 16-byte atomics we also use 8-byte memory access,
  // this leads to false negatives only in very obscure cases.
}

#if !SANITIZER_GO
static atomic_uint8_t *to_atomic(const volatile a8 *a) {
  return reinterpret_cast<atomic_uint8_t *>(const_cast<a8 *>(a));
}

static atomic_uint16_t *to_atomic(const volatile a16 *a) {
  return reinterpret_cast<atomic_uint16_t *>(const_cast<a16 *>(a));
}
#endif

static atomic_uint32_t *to_atomic(const volatile a32 *a) {
  return reinterpret_cast<atomic_uint32_t *>(const_cast<a32 *>(a));
}

static atomic_uint64_t *to_atomic(const volatile a64 *a) {
  return reinterpret_cast<atomic_uint64_t *>(const_cast<a64 *>(a));
}

static memory_order to_mo(morder mo) {
  switch (mo) {
  case mo_relaxed: return memory_order_relaxed;
  case mo_consume: return memory_order_consume;
  case mo_acquire: return memory_order_acquire;
  case mo_release: return memory_order_release;
  case mo_acq_rel: return memory_order_acq_rel;
  case mo_seq_cst: return memory_order_seq_cst;
  }
  DCHECK(0);
  return memory_order_seq_cst;
}

template<typename T>
static T NoTsanAtomicLoad(const volatile T *a, morder mo) {
  return atomic_load(to_atomic(a), to_mo(mo));
}

#if __TSAN_HAS_INT128 && !SANITIZER_GO
static a128 NoTsanAtomicLoad(const volatile a128 *a, morder mo) {
  SpinMutexLock lock(&mutex128);
  return *a;
}
#endif

template <typename T>
static T AtomicLoad(ThreadState *thr, uptr pc, const volatile T *a, morder mo) {
  DCHECK(IsLoadOrder(mo));
  // This fast-path is critical for performance.
  // Assume the access is atomic.
  if (!IsAcquireOrder(mo)) {
    MemoryAccess(thr, pc, (uptr)a, AccessSize<T>(),
                 kAccessRead | kAccessAtomic);
    return NoTsanAtomicLoad(a, mo);
  }
  // Don't create sync object if it does not exist yet. For example, an atomic
  // pointer is initialized to nullptr and then periodically acquire-loaded.
  T v = NoTsanAtomicLoad(a, mo);
  SyncVar *s = ctx->metamap.GetSyncIfExists((uptr)a);
  if (s) {
    SlotLocker locker(thr);
    ReadLock lock(&s->mtx);
    thr->clock.Acquire(s->clock);
    // Re-read under sync mutex because we need a consistent snapshot
    // of the value and the clock we acquire.
    v = NoTsanAtomicLoad(a, mo);
  }
  MemoryAccess(thr, pc, (uptr)a, AccessSize<T>(), kAccessRead | kAccessAtomic);
  return v;
}

template<typename T>
static void NoTsanAtomicStore(volatile T *a, T v, morder mo) {
  atomic_store(to_atomic(a), v, to_mo(mo));
}

#if __TSAN_HAS_INT128 && !SANITIZER_GO
static void NoTsanAtomicStore(volatile a128 *a, a128 v, morder mo) {
  SpinMutexLock lock(&mutex128);
  *a = v;
}
#endif

template <typename T>
static void AtomicStore(ThreadState *thr, uptr pc, volatile T *a, T v,
                        morder mo) {
  DCHECK(IsStoreOrder(mo));
  MemoryAccess(thr, pc, (uptr)a, AccessSize<T>(), kAccessWrite | kAccessAtomic);
  // This fast-path is critical for performance.
  // Assume the access is atomic.
  // Strictly saying even relaxed store cuts off release sequence,
  // so must reset the clock.
  if (!IsReleaseOrder(mo)) {
    NoTsanAtomicStore(a, v, mo);
    return;
  }
  SlotLocker locker(thr);
  {
    auto s = ctx->metamap.GetSyncOrCreate(thr, pc, (uptr)a, false);
    Lock lock(&s->mtx);
    thr->clock.ReleaseStore(&s->clock);
    NoTsanAtomicStore(a, v, mo);
  }
  IncrementEpoch(thr);
}

template <typename T, T (*F)(volatile T *v, T op)>
static T AtomicRMW(ThreadState *thr, uptr pc, volatile T *a, T v, morder mo) {
  MemoryAccess(thr, pc, (uptr)a, AccessSize<T>(), kAccessWrite | kAccessAtomic);
  if (LIKELY(mo == mo_relaxed))
    return F(a, v);
  SlotLocker locker(thr);
  {
    auto s = ctx->metamap.GetSyncOrCreate(thr, pc, (uptr)a, false);
    RWLock lock(&s->mtx, IsReleaseOrder(mo));
    if (IsAcqRelOrder(mo))
      thr->clock.ReleaseAcquire(&s->clock);
    else if (IsReleaseOrder(mo))
      thr->clock.Release(&s->clock);
    else if (IsAcquireOrder(mo))
      thr->clock.Acquire(s->clock);
    v = F(a, v);
  }
  if (IsReleaseOrder(mo))
    IncrementEpoch(thr);
  return v;
}

template<typename T>
static T NoTsanAtomicExchange(volatile T *a, T v, morder mo) {
  return func_xchg(a, v);
}

template<typename T>
static T NoTsanAtomicFetchAdd(volatile T *a, T v, morder mo) {
  return func_add(a, v);
}

template<typename T>
static T NoTsanAtomicFetchSub(volatile T *a, T v, morder mo) {
  return func_sub(a, v);
}

template<typename T>
static T NoTsanAtomicFetchAnd(volatile T *a, T v, morder mo) {
  return func_and(a, v);
}

template<typename T>
static T NoTsanAtomicFetchOr(volatile T *a, T v, morder mo) {
  return func_or(a, v);
}

template<typename T>
static T NoTsanAtomicFetchXor(volatile T *a, T v, morder mo) {
  return func_xor(a, v);
}

template<typename T>
static T NoTsanAtomicFetchNand(volatile T *a, T v, morder mo) {
  return func_nand(a, v);
}

template<typename T>
static T AtomicExchange(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_xchg>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchAdd(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_add>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchSub(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_sub>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchAnd(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_and>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchOr(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_or>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchXor(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_xor>(thr, pc, a, v, mo);
}

template<typename T>
static T AtomicFetchNand(ThreadState *thr, uptr pc, volatile T *a, T v,
    morder mo) {
  return AtomicRMW<T, func_nand>(thr, pc, a, v, mo);
}

template<typename T>
static bool NoTsanAtomicCAS(volatile T *a, T *c, T v, morder mo, morder fmo) {
  return atomic_compare_exchange_strong(to_atomic(a), c, v, to_mo(mo));
}

#if __TSAN_HAS_INT128
static bool NoTsanAtomicCAS(volatile a128 *a, a128 *c, a128 v,
    morder mo, morder fmo) {
  a128 old = *c;
  a128 cur = func_cas(a, old, v);
  if (cur == old)
    return true;
  *c = cur;
  return false;
}
#endif

template<typename T>
static T NoTsanAtomicCAS(volatile T *a, T c, T v, morder mo, morder fmo) {
  NoTsanAtomicCAS(a, &c, v, mo, fmo);
  return c;
}

template <typename T>
static bool AtomicCAS(ThreadState *thr, uptr pc, volatile T *a, T *c, T v,
                      morder mo, morder fmo) {
  // 31.7.2.18: "The failure argument shall not be memory_order_release
  // nor memory_order_acq_rel". LLVM (2021-05) fallbacks to Monotonic
  // (mo_relaxed) when those are used.
  DCHECK(IsLoadOrder(fmo));

  MemoryAccess(thr, pc, (uptr)a, AccessSize<T>(), kAccessWrite | kAccessAtomic);
  if (LIKELY(mo == mo_relaxed && fmo == mo_relaxed)) {
    T cc = *c;
    T pr = func_cas(a, cc, v);
    if (pr == cc)
      return true;
    *c = pr;
    return false;
  }
  SlotLocker locker(thr);
  bool release = IsReleaseOrder(mo);
  bool success;
  {
    auto s = ctx->metamap.GetSyncOrCreate(thr, pc, (uptr)a, false);
    RWLock lock(&s->mtx, release);
    T cc = *c;
    T pr = func_cas(a, cc, v);
    success = pr == cc;
    if (!success) {
      *c = pr;
      mo = fmo;
    }
    if (success && IsAcqRelOrder(mo))
      thr->clock.ReleaseAcquire(&s->clock);
    else if (success && IsReleaseOrder(mo))
      thr->clock.Release(&s->clock);
    else if (IsAcquireOrder(mo))
      thr->clock.Acquire(s->clock);
  }
  if (success && release)
    IncrementEpoch(thr);
  return success;
}

template<typename T>
static T AtomicCAS(ThreadState *thr, uptr pc,
    volatile T *a, T c, T v, morder mo, morder fmo) {
  AtomicCAS(thr, pc, a, &c, v, mo, fmo);
  return c;
}

#if !SANITIZER_GO
static void NoTsanAtomicFence(morder mo) {
  __sync_synchronize();
}

static void AtomicFence(ThreadState *thr, uptr pc, morder mo) {
  // FIXME(dvyukov): not implemented.
  __sync_synchronize();
}
#endif

// Interface functions follow.
#if !SANITIZER_GO

// C/C++

static morder convert_morder(morder mo) {
  if (flags()->force_seq_cst_atomics)
    return (morder)mo_seq_cst;

  // Filter out additional memory order flags:
  // MEMMODEL_SYNC        = 1 << 15
  // __ATOMIC_HLE_ACQUIRE = 1 << 16
  // __ATOMIC_HLE_RELEASE = 1 << 17
  //
  // HLE is an optimization, and we pretend that elision always fails.
  // MEMMODEL_SYNC is used when lowering __sync_ atomics,
  // since we use __sync_ atomics for actual atomic operations,
  // we can safely ignore it as well. It also subtly affects semantics,
  // but we don't model the difference.
  return (morder)(mo & 0x7fff);
}

#  define ATOMIC_IMPL(func, ...)                                \
    ThreadState *const thr = cur_thread();                      \
    ProcessPendingSignals(thr);                                 \
    if (UNLIKELY(thr->ignore_sync || thr->ignore_interceptors)) \
      return NoTsanAtomic##func(__VA_ARGS__);                   \
    mo = convert_morder(mo);                                    \
    return Atomic##func(thr, GET_CALLER_PC(), __VA_ARGS__);

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_load(const volatile a8 *a, morder mo) {
  ATOMIC_IMPL(Load, a, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_load(const volatile a16 *a, morder mo) {
  ATOMIC_IMPL(Load, a, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_load(const volatile a32 *a, morder mo) {
  ATOMIC_IMPL(Load, a, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_load(const volatile a64 *a, morder mo) {
  ATOMIC_IMPL(Load, a, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_load(const volatile a128 *a, morder mo) {
  ATOMIC_IMPL(Load, a, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic8_store(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(Store, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic16_store(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(Store, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic32_store(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(Store, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic64_store(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(Store, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic128_store(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(Store, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_exchange(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(Exchange, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_exchange(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(Exchange, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_exchange(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(Exchange, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_exchange(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(Exchange, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_exchange(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(Exchange, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_add(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchAdd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_add(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchAdd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_add(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchAdd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_add(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchAdd, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_add(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchAdd, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_sub(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchSub, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_sub(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchSub, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_sub(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchSub, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_sub(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchSub, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_sub(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchSub, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_and(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchAnd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_and(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchAnd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_and(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchAnd, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_and(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchAnd, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_and(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchAnd, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_or(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchOr, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_or(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchOr, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_or(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchOr, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_or(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchOr, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_or(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchOr, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_xor(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchXor, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_xor(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchXor, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_xor(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchXor, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_xor(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchXor, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_xor(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchXor, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_fetch_nand(volatile a8 *a, a8 v, morder mo) {
  ATOMIC_IMPL(FetchNand, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_fetch_nand(volatile a16 *a, a16 v, morder mo) {
  ATOMIC_IMPL(FetchNand, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_fetch_nand(volatile a32 *a, a32 v, morder mo) {
  ATOMIC_IMPL(FetchNand, a, v, mo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_fetch_nand(volatile a64 *a, a64 v, morder mo) {
  ATOMIC_IMPL(FetchNand, a, v, mo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_fetch_nand(volatile a128 *a, a128 v, morder mo) {
  ATOMIC_IMPL(FetchNand, a, v, mo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic8_compare_exchange_strong(volatile a8 *a, a8 *c, a8 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic16_compare_exchange_strong(volatile a16 *a, a16 *c, a16 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic32_compare_exchange_strong(volatile a32 *a, a32 *c, a32 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic64_compare_exchange_strong(volatile a64 *a, a64 *c, a64 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic128_compare_exchange_strong(volatile a128 *a, a128 *c, a128 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic8_compare_exchange_weak(volatile a8 *a, a8 *c, a8 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic16_compare_exchange_weak(volatile a16 *a, a16 *c, a16 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic32_compare_exchange_weak(volatile a32 *a, a32 *c, a32 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic64_compare_exchange_weak(volatile a64 *a, a64 *c, a64 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_atomic128_compare_exchange_weak(volatile a128 *a, a128 *c, a128 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
a8 __tsan_atomic8_compare_exchange_val(volatile a8 *a, a8 c, a8 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a16 __tsan_atomic16_compare_exchange_val(volatile a16 *a, a16 c, a16 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a32 __tsan_atomic32_compare_exchange_val(volatile a32 *a, a32 c, a32 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

SANITIZER_INTERFACE_ATTRIBUTE
a64 __tsan_atomic64_compare_exchange_val(volatile a64 *a, a64 c, a64 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}

#if __TSAN_HAS_INT128
SANITIZER_INTERFACE_ATTRIBUTE
a128 __tsan_atomic128_compare_exchange_val(volatile a128 *a, a128 c, a128 v,
    morder mo, morder fmo) {
  ATOMIC_IMPL(CAS, a, c, v, mo, fmo);
}
#endif

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic_thread_fence(morder mo) { ATOMIC_IMPL(Fence, mo); }

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_atomic_signal_fence(morder mo) {
}
}  // extern "C"

#else  // #if !SANITIZER_GO

// Go

#  define ATOMIC(func, ...)               \
    if (thr->ignore_sync) {               \
      NoTsanAtomic##func(__VA_ARGS__);    \
    } else {                              \
      FuncEntry(thr, cpc);                \
      Atomic##func(thr, pc, __VA_ARGS__); \
      FuncExit(thr);                      \
    }

#  define ATOMIC_RET(func, ret, ...)              \
    if (thr->ignore_sync) {                       \
      (ret) = NoTsanAtomic##func(__VA_ARGS__);    \
    } else {                                      \
      FuncEntry(thr, cpc);                        \
      (ret) = Atomic##func(thr, pc, __VA_ARGS__); \
      FuncExit(thr);                              \
    }

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_load(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(Load, *(a32*)(a+8), *(a32**)a, mo_acquire);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_load(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(Load, *(a64*)(a+8), *(a64**)a, mo_acquire);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_store(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC(Store, *(a32**)a, *(a32*)(a+8), mo_release);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_store(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC(Store, *(a64**)a, *(a64*)(a+8), mo_release);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_fetch_add(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchAdd, *(a32*)(a+16), *(a32**)a, *(a32*)(a+8), mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_fetch_add(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchAdd, *(a64*)(a+16), *(a64**)a, *(a64*)(a+8), mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_fetch_and(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchAnd, *(a32 *)(a + 16), *(a32 **)a, *(a32 *)(a + 8),
             mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_fetch_and(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchAnd, *(a64 *)(a + 16), *(a64 **)a, *(a64 *)(a + 8),
             mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_fetch_or(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchOr, *(a32 *)(a + 16), *(a32 **)a, *(a32 *)(a + 8),
             mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_fetch_or(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(FetchOr, *(a64 *)(a + 16), *(a64 **)a, *(a64 *)(a + 8),
             mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_exchange(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(Exchange, *(a32*)(a+16), *(a32**)a, *(a32*)(a+8), mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_exchange(ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  ATOMIC_RET(Exchange, *(a64*)(a+16), *(a64**)a, *(a64*)(a+8), mo_acq_rel);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic32_compare_exchange(
    ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  a32 cur = 0;
  a32 cmp = *(a32*)(a+8);
  ATOMIC_RET(CAS, cur, *(a32**)a, cmp, *(a32*)(a+12), mo_acq_rel, mo_acquire);
  *(bool*)(a+16) = (cur == cmp);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_go_atomic64_compare_exchange(
    ThreadState *thr, uptr cpc, uptr pc, u8 *a) {
  a64 cur = 0;
  a64 cmp = *(a64*)(a+8);
  ATOMIC_RET(CAS, cur, *(a64**)a, cmp, *(a64*)(a+16), mo_acq_rel, mo_acquire);
  *(bool*)(a+24) = (cur == cmp);
}
}  // extern "C"
#endif  // #if !SANITIZER_GO
