//===-- sanitizer_mutex.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_MUTEX_H
#define SANITIZER_MUTEX_H

#include "sanitizer_atomic.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_thread_safety.h"

namespace __sanitizer {

class SANITIZER_MUTEX StaticSpinMutex {
 public:
  void Init() {
    atomic_store(&state_, 0, memory_order_relaxed);
  }

  void Lock() SANITIZER_ACQUIRE() {
    if (LIKELY(TryLock()))
      return;
    LockSlow();
  }

  bool TryLock() SANITIZER_TRY_ACQUIRE(true) {
    return atomic_exchange(&state_, 1, memory_order_acquire) == 0;
  }

  void Unlock() SANITIZER_RELEASE() {
    atomic_store(&state_, 0, memory_order_release);
  }

  void CheckLocked() const SANITIZER_CHECK_LOCKED() {
    CHECK_EQ(atomic_load(&state_, memory_order_relaxed), 1);
  }

 private:
  atomic_uint8_t state_;

  void LockSlow();
};

class SANITIZER_MUTEX SpinMutex : public StaticSpinMutex {
 public:
  SpinMutex() {
    Init();
  }

  SpinMutex(const SpinMutex &) = delete;
  void operator=(const SpinMutex &) = delete;
};

// Semaphore provides an OS-dependent way to park/unpark threads.
// The last thread returned from Wait can destroy the object
// (destruction-safety).
class Semaphore {
 public:
  constexpr Semaphore() {}
  Semaphore(const Semaphore &) = delete;
  void operator=(const Semaphore &) = delete;

  void Wait();
  void Post(u32 count = 1);

 private:
  atomic_uint32_t state_ = {0};
};

typedef int MutexType;

enum {
  // Used as sentinel and to catch unassigned types
  // (should not be used as real Mutex type).
  MutexInvalid = 0,
  MutexThreadRegistry,
  // Each tool own mutexes must start at this number.
  MutexLastCommon,
  // Type for legacy mutexes that are not checked for deadlocks.
  MutexUnchecked = -1,
  // Special marks that can be used in MutexMeta::can_lock table.
  // The leaf mutexes can be locked under any other non-leaf mutex,
  // but no other mutex can be locked while under a leaf mutex.
  MutexLeaf = -1,
  // Multiple mutexes of this type can be locked at the same time.
  MutexMulti = -3,
};

// Go linker does not support THREADLOCAL variables,
// so we can't use per-thread state.
// Disable checked locks on Darwin. Although Darwin platforms support
// THREADLOCAL variables they are not usable early on during process init when
// `__sanitizer::Mutex` is used.
#define SANITIZER_CHECK_DEADLOCKS \
  (SANITIZER_DEBUG && !SANITIZER_GO && SANITIZER_SUPPORTS_THREADLOCAL && !SANITIZER_APPLE)

#if SANITIZER_CHECK_DEADLOCKS
struct MutexMeta {
  MutexType type;
  const char *name;
  // The table fixes what mutexes can be locked under what mutexes.
  // If the entry for MutexTypeFoo contains MutexTypeBar,
  // then Bar mutex can be locked while under Foo mutex.
  // Can also contain the special MutexLeaf/MutexMulti marks.
  MutexType can_lock[10];
};
#endif

class CheckedMutex {
 public:
  explicit constexpr CheckedMutex(MutexType type)
#if SANITIZER_CHECK_DEADLOCKS
      : type_(type)
#endif
  {
  }

  ALWAYS_INLINE void Lock() {
#if SANITIZER_CHECK_DEADLOCKS
    LockImpl(GET_CALLER_PC());
#endif
  }

  ALWAYS_INLINE void Unlock() {
#if SANITIZER_CHECK_DEADLOCKS
    UnlockImpl();
#endif
  }

  // Checks that the current thread does not hold any mutexes
  // (e.g. when returning from a runtime function to user code).
  static void CheckNoLocks() {
#if SANITIZER_CHECK_DEADLOCKS
    CheckNoLocksImpl();
#endif
  }

 private:
#if SANITIZER_CHECK_DEADLOCKS
  const MutexType type_;

  void LockImpl(uptr pc);
  void UnlockImpl();
  static void CheckNoLocksImpl();
#endif
};

// Reader-writer mutex.
// Derive from CheckedMutex for the purposes of EBO.
// We could make it a field marked with [[no_unique_address]],
// but this attribute is not supported by some older compilers.
class SANITIZER_MUTEX Mutex : CheckedMutex {
 public:
  explicit constexpr Mutex(MutexType type = MutexUnchecked)
      : CheckedMutex(type) {}

  void Lock() SANITIZER_ACQUIRE() {
    CheckedMutex::Lock();
    u64 reset_mask = ~0ull;
    u64 state = atomic_load_relaxed(&state_);
    for (uptr spin_iters = 0;; spin_iters++) {
      u64 new_state;
      bool locked = (state & (kWriterLock | kReaderLockMask)) != 0;
      if (LIKELY(!locked)) {
        // The mutex is not read-/write-locked, try to lock.
        new_state = (state | kWriterLock) & reset_mask;
      } else if (spin_iters > kMaxSpinIters) {
        // We've spun enough, increment waiting writers count and block.
        // The counter will be decremented by whoever wakes us.
        new_state = (state + kWaitingWriterInc) & reset_mask;
      } else if ((state & kWriterSpinWait) == 0) {
        // Active spinning, but denote our presence so that unlocking
        // thread does not wake up other threads.
        new_state = state | kWriterSpinWait;
      } else {
        // Active spinning.
        state = atomic_load(&state_, memory_order_relaxed);
        continue;
      }
      if (UNLIKELY(!atomic_compare_exchange_weak(&state_, &state, new_state,
                                                 memory_order_acquire)))
        continue;
      if (LIKELY(!locked))
        return;  // We've locked the mutex.
      if (spin_iters > kMaxSpinIters) {
        // We've incremented waiting writers, so now block.
        writers_.Wait();
        spin_iters = 0;
      } else {
        // We've set kWriterSpinWait, but we are still in active spinning.
      }
      // We either blocked and were unblocked,
      // or we just spun but set kWriterSpinWait.
      // Either way we need to reset kWriterSpinWait
      // next time we take the lock or block again.
      reset_mask = ~kWriterSpinWait;
      state = atomic_load(&state_, memory_order_relaxed);
      DCHECK_NE(state & kWriterSpinWait, 0);
    }
  }

  bool TryLock() SANITIZER_TRY_ACQUIRE(true) {
    u64 state = atomic_load_relaxed(&state_);
    for (;;) {
      if (UNLIKELY(state & (kWriterLock | kReaderLockMask)))
        return false;
      // The mutex is not read-/write-locked, try to lock.
      if (LIKELY(atomic_compare_exchange_weak(
              &state_, &state, state | kWriterLock, memory_order_acquire))) {
        CheckedMutex::Lock();
        return true;
      }
    }
  }

  void Unlock() SANITIZER_RELEASE() {
    CheckedMutex::Unlock();
    bool wake_writer;
    u64 wake_readers;
    u64 new_state;
    u64 state = atomic_load_relaxed(&state_);
    do {
      DCHECK_NE(state & kWriterLock, 0);
      DCHECK_EQ(state & kReaderLockMask, 0);
      new_state = state & ~kWriterLock;
      wake_writer = (state & (kWriterSpinWait | kReaderSpinWait)) == 0 &&
                    (state & kWaitingWriterMask) != 0;
      if (wake_writer)
        new_state = (new_state - kWaitingWriterInc) | kWriterSpinWait;
      wake_readers =
          wake_writer || (state & kWriterSpinWait) != 0
              ? 0
              : ((state & kWaitingReaderMask) >> kWaitingReaderShift);
      if (wake_readers)
        new_state = (new_state & ~kWaitingReaderMask) | kReaderSpinWait;
    } while (UNLIKELY(!atomic_compare_exchange_weak(&state_, &state, new_state,
                                                    memory_order_release)));
    if (UNLIKELY(wake_writer))
      writers_.Post();
    else if (UNLIKELY(wake_readers))
      readers_.Post(wake_readers);
  }

  void ReadLock() SANITIZER_ACQUIRE_SHARED() {
    CheckedMutex::Lock();
    u64 reset_mask = ~0ull;
    u64 state = atomic_load_relaxed(&state_);
    for (uptr spin_iters = 0;; spin_iters++) {
      bool locked = (state & kWriterLock) != 0;
      u64 new_state;
      if (LIKELY(!locked)) {
        new_state = (state + kReaderLockInc) & reset_mask;
      } else if (spin_iters > kMaxSpinIters) {
        new_state = (state + kWaitingReaderInc) & reset_mask;
      } else if ((state & kReaderSpinWait) == 0) {
        // Active spinning, but denote our presence so that unlocking
        // thread does not wake up other threads.
        new_state = state | kReaderSpinWait;
      } else {
        // Active spinning.
        state = atomic_load(&state_, memory_order_relaxed);
        continue;
      }
      if (UNLIKELY(!atomic_compare_exchange_weak(&state_, &state, new_state,
                                                 memory_order_acquire)))
        continue;
      if (LIKELY(!locked))
        return;  // We've locked the mutex.
      if (spin_iters > kMaxSpinIters) {
        // We've incremented waiting readers, so now block.
        readers_.Wait();
        spin_iters = 0;
      } else {
        // We've set kReaderSpinWait, but we are still in active spinning.
      }
      reset_mask = ~kReaderSpinWait;
      state = atomic_load(&state_, memory_order_relaxed);
    }
  }

  void ReadUnlock() SANITIZER_RELEASE_SHARED() {
    CheckedMutex::Unlock();
    bool wake;
    u64 new_state;
    u64 state = atomic_load_relaxed(&state_);
    do {
      DCHECK_NE(state & kReaderLockMask, 0);
      DCHECK_EQ(state & kWriterLock, 0);
      new_state = state - kReaderLockInc;
      wake = (new_state &
              (kReaderLockMask | kWriterSpinWait | kReaderSpinWait)) == 0 &&
             (new_state & kWaitingWriterMask) != 0;
      if (wake)
        new_state = (new_state - kWaitingWriterInc) | kWriterSpinWait;
    } while (UNLIKELY(!atomic_compare_exchange_weak(&state_, &state, new_state,
                                                    memory_order_release)));
    if (UNLIKELY(wake))
      writers_.Post();
  }

  // This function does not guarantee an explicit check that the calling thread
  // is the thread which owns the mutex. This behavior, while more strictly
  // correct, causes problems in cases like StopTheWorld, where a parent thread
  // owns the mutex but a child checks that it is locked. Rather than
  // maintaining complex state to work around those situations, the check only
  // checks that the mutex is owned.
  void CheckWriteLocked() const SANITIZER_CHECK_LOCKED() {
    CHECK(atomic_load(&state_, memory_order_relaxed) & kWriterLock);
  }

  void CheckLocked() const SANITIZER_CHECK_LOCKED() { CheckWriteLocked(); }

  void CheckReadLocked() const SANITIZER_CHECK_LOCKED() {
    CHECK(atomic_load(&state_, memory_order_relaxed) & kReaderLockMask);
  }

 private:
  atomic_uint64_t state_ = {0};
  Semaphore writers_;
  Semaphore readers_;

  // The state has 3 counters:
  //  - number of readers holding the lock,
  //    if non zero, the mutex is read-locked
  //  - number of waiting readers,
  //    if not zero, the mutex is write-locked
  //  - number of waiting writers,
  //    if non zero, the mutex is read- or write-locked
  // And 2 flags:
  //  - writer lock
  //    if set, the mutex is write-locked
  //  - a writer is awake and spin-waiting
  //    the flag is used to prevent thundering herd problem
  //    (new writers are not woken if this flag is set)
  //  - a reader is awake and spin-waiting
  //
  // Both writers and readers use active spinning before blocking.
  // But readers are more aggressive and always take the mutex
  // if there are any other readers.
  // After wake up both writers and readers compete to lock the
  // mutex again. This is needed to allow repeated locks even in presence
  // of other blocked threads.
  static constexpr u64 kCounterWidth = 20;
  static constexpr u64 kReaderLockShift = 0;
  static constexpr u64 kReaderLockInc = 1ull << kReaderLockShift;
  static constexpr u64 kReaderLockMask = ((1ull << kCounterWidth) - 1)
                                         << kReaderLockShift;
  static constexpr u64 kWaitingReaderShift = kCounterWidth;
  static constexpr u64 kWaitingReaderInc = 1ull << kWaitingReaderShift;
  static constexpr u64 kWaitingReaderMask = ((1ull << kCounterWidth) - 1)
                                            << kWaitingReaderShift;
  static constexpr u64 kWaitingWriterShift = 2 * kCounterWidth;
  static constexpr u64 kWaitingWriterInc = 1ull << kWaitingWriterShift;
  static constexpr u64 kWaitingWriterMask = ((1ull << kCounterWidth) - 1)
                                            << kWaitingWriterShift;
  static constexpr u64 kWriterLock = 1ull << (3 * kCounterWidth);
  static constexpr u64 kWriterSpinWait = 1ull << (3 * kCounterWidth + 1);
  static constexpr u64 kReaderSpinWait = 1ull << (3 * kCounterWidth + 2);

  static constexpr uptr kMaxSpinIters = 1500;

  Mutex(LinkerInitialized) = delete;
  Mutex(const Mutex &) = delete;
  void operator=(const Mutex &) = delete;
};

void FutexWait(atomic_uint32_t *p, u32 cmp);
void FutexWake(atomic_uint32_t *p, u32 count);

template <typename MutexType>
class SANITIZER_SCOPED_LOCK GenericScopedLock {
 public:
  explicit GenericScopedLock(MutexType *mu) SANITIZER_ACQUIRE(mu) : mu_(mu) {
    mu_->Lock();
  }

  ~GenericScopedLock() SANITIZER_RELEASE() { mu_->Unlock(); }

 private:
  MutexType *mu_;

  GenericScopedLock(const GenericScopedLock &) = delete;
  void operator=(const GenericScopedLock &) = delete;
};

template <typename MutexType>
class SANITIZER_SCOPED_LOCK GenericScopedReadLock {
 public:
  explicit GenericScopedReadLock(MutexType *mu) SANITIZER_ACQUIRE(mu)
      : mu_(mu) {
    mu_->ReadLock();
  }

  ~GenericScopedReadLock() SANITIZER_RELEASE() { mu_->ReadUnlock(); }

 private:
  MutexType *mu_;

  GenericScopedReadLock(const GenericScopedReadLock &) = delete;
  void operator=(const GenericScopedReadLock &) = delete;
};

template <typename MutexType>
class SANITIZER_SCOPED_LOCK GenericScopedRWLock {
 public:
  ALWAYS_INLINE explicit GenericScopedRWLock(MutexType *mu, bool write)
      SANITIZER_ACQUIRE(mu)
      : mu_(mu), write_(write) {
    if (write_)
      mu_->Lock();
    else
      mu_->ReadLock();
  }

  ALWAYS_INLINE ~GenericScopedRWLock() SANITIZER_RELEASE() {
    if (write_)
      mu_->Unlock();
    else
      mu_->ReadUnlock();
  }

 private:
  MutexType *mu_;
  bool write_;

  GenericScopedRWLock(const GenericScopedRWLock &) = delete;
  void operator=(const GenericScopedRWLock &) = delete;
};

typedef GenericScopedLock<StaticSpinMutex> SpinMutexLock;
typedef GenericScopedLock<Mutex> Lock;
typedef GenericScopedReadLock<Mutex> ReadLock;
typedef GenericScopedRWLock<Mutex> RWLock;

}  // namespace __sanitizer

#endif  // SANITIZER_MUTEX_H
