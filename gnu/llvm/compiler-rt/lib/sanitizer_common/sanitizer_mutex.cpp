//===-- sanitizer_mutex.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_mutex.h"

#include "sanitizer_common.h"

namespace __sanitizer {

void StaticSpinMutex::LockSlow() {
  for (int i = 0;; i++) {
    if (i < 100)
      proc_yield(1);
    else
      internal_sched_yield();
    if (atomic_load(&state_, memory_order_relaxed) == 0 &&
        atomic_exchange(&state_, 1, memory_order_acquire) == 0)
      return;
  }
}

void Semaphore::Wait() {
  u32 count = atomic_load(&state_, memory_order_relaxed);
  for (;;) {
    if (count == 0) {
      FutexWait(&state_, 0);
      count = atomic_load(&state_, memory_order_relaxed);
      continue;
    }
    if (atomic_compare_exchange_weak(&state_, &count, count - 1,
                                     memory_order_acquire))
      break;
  }
}

void Semaphore::Post(u32 count) {
  CHECK_NE(count, 0);
  atomic_fetch_add(&state_, count, memory_order_release);
  FutexWake(&state_, count);
}

#if SANITIZER_CHECK_DEADLOCKS
// An empty mutex meta table, it effectively disables deadlock detection.
// Each tool can override the table to define own mutex hierarchy and
// enable deadlock detection.
// The table defines a static mutex type hierarchy (what mutex types can be locked
// under what mutex types). This table is checked to be acyclic and then
// actual mutex lock/unlock operations are checked to adhere to this hierarchy.
// The checking happens on mutex types rather than on individual mutex instances
// because doing it on mutex instances will both significantly complicate
// the implementation, worsen performance and memory overhead and is mostly
// unnecessary (we almost never lock multiple mutexes of the same type recursively).
static constexpr int kMutexTypeMax = 20;
SANITIZER_WEAK_ATTRIBUTE MutexMeta mutex_meta[kMutexTypeMax] = {};
SANITIZER_WEAK_ATTRIBUTE void PrintMutexPC(uptr pc) {}
static StaticSpinMutex mutex_meta_mtx;
static int mutex_type_count = -1;
// Adjacency matrix of what mutexes can be locked under what mutexes.
static bool mutex_can_lock[kMutexTypeMax][kMutexTypeMax];
// Mutex types with MutexMulti mark.
static bool mutex_multi[kMutexTypeMax];

void DebugMutexInit() {
  // Build adjacency matrix.
  bool leaf[kMutexTypeMax];
  internal_memset(&leaf, 0, sizeof(leaf));
  int cnt[kMutexTypeMax];
  internal_memset(&cnt, 0, sizeof(cnt));
  for (int t = 0; t < kMutexTypeMax; t++) {
    mutex_type_count = t;
    if (!mutex_meta[t].name)
      break;
    CHECK_EQ(t, mutex_meta[t].type);
    for (uptr j = 0; j < ARRAY_SIZE(mutex_meta[t].can_lock); j++) {
      MutexType z = mutex_meta[t].can_lock[j];
      if (z == MutexInvalid)
        break;
      if (z == MutexLeaf) {
        CHECK(!leaf[t]);
        leaf[t] = true;
        continue;
      }
      if (z == MutexMulti) {
        mutex_multi[t] = true;
        continue;
      }
      CHECK_LT(z, kMutexTypeMax);
      CHECK(!mutex_can_lock[t][z]);
      mutex_can_lock[t][z] = true;
      cnt[t]++;
    }
  }
  // Indicates the array is not properly terminated.
  CHECK_LT(mutex_type_count, kMutexTypeMax);
  // Add leaf mutexes.
  for (int t = 0; t < mutex_type_count; t++) {
    if (!leaf[t])
      continue;
    CHECK_EQ(cnt[t], 0);
    for (int z = 0; z < mutex_type_count; z++) {
      if (z == MutexInvalid || t == z || leaf[z])
        continue;
      CHECK(!mutex_can_lock[z][t]);
      mutex_can_lock[z][t] = true;
    }
  }
  // Build the transitive closure and check that the graphs is acyclic.
  u32 trans[kMutexTypeMax];
  static_assert(sizeof(trans[0]) * 8 >= kMutexTypeMax,
                "kMutexTypeMax does not fit into u32, switch to u64");
  internal_memset(&trans, 0, sizeof(trans));
  for (int i = 0; i < mutex_type_count; i++) {
    for (int j = 0; j < mutex_type_count; j++)
      if (mutex_can_lock[i][j])
        trans[i] |= 1 << j;
  }
  for (int k = 0; k < mutex_type_count; k++) {
    for (int i = 0; i < mutex_type_count; i++) {
      if (trans[i] & (1 << k))
        trans[i] |= trans[k];
    }
  }
  for (int i = 0; i < mutex_type_count; i++) {
    if (trans[i] & (1 << i)) {
      Printf("Mutex %s participates in a cycle\n", mutex_meta[i].name);
      Die();
    }
  }
}

struct InternalDeadlockDetector {
  struct LockDesc {
    u64 seq;
    uptr pc;
    int recursion;
  };
  int initialized;
  u64 sequence;
  LockDesc locked[kMutexTypeMax];

  void Lock(MutexType type, uptr pc) {
    if (!Initialize(type))
      return;
    CHECK_LT(type, mutex_type_count);
    // Find the last locked mutex type.
    // This is the type we will use for hierarchy checks.
    u64 max_seq = 0;
    MutexType max_idx = MutexInvalid;
    for (int i = 0; i != mutex_type_count; i++) {
      if (locked[i].seq == 0)
        continue;
      CHECK_NE(locked[i].seq, max_seq);
      if (max_seq < locked[i].seq) {
        max_seq = locked[i].seq;
        max_idx = (MutexType)i;
      }
    }
    if (max_idx == type && mutex_multi[type]) {
      // Recursive lock of the same type.
      CHECK_EQ(locked[type].seq, max_seq);
      CHECK(locked[type].pc);
      locked[type].recursion++;
      return;
    }
    if (max_idx != MutexInvalid && !mutex_can_lock[max_idx][type]) {
      Printf("%s: internal deadlock: can't lock %s under %s mutex\n", SanitizerToolName,
             mutex_meta[type].name, mutex_meta[max_idx].name);
      PrintMutexPC(locked[max_idx].pc);
      CHECK(0);
    }
    locked[type].seq = ++sequence;
    locked[type].pc = pc;
    locked[type].recursion = 1;
  }

  void Unlock(MutexType type) {
    if (!Initialize(type))
      return;
    CHECK_LT(type, mutex_type_count);
    CHECK(locked[type].seq);
    CHECK_GT(locked[type].recursion, 0);
    if (--locked[type].recursion)
      return;
    locked[type].seq = 0;
    locked[type].pc = 0;
  }

  void CheckNoLocks() {
    for (int i = 0; i < mutex_type_count; i++) CHECK_EQ(locked[i].recursion, 0);
  }

  bool Initialize(MutexType type) {
    if (type == MutexUnchecked || type == MutexInvalid)
      return false;
    CHECK_GT(type, MutexInvalid);
    if (initialized != 0)
      return initialized > 0;
    initialized = -1;
    SpinMutexLock lock(&mutex_meta_mtx);
    if (mutex_type_count < 0)
      DebugMutexInit();
    initialized = mutex_type_count ? 1 : -1;
    return initialized > 0;
  }
};
// This variable is used by the __tls_get_addr interceptor, so cannot use the
// global-dynamic TLS model, as that would result in crashes.
__attribute__((tls_model("initial-exec"))) static THREADLOCAL
    InternalDeadlockDetector deadlock_detector;

void CheckedMutex::LockImpl(uptr pc) { deadlock_detector.Lock(type_, pc); }

void CheckedMutex::UnlockImpl() { deadlock_detector.Unlock(type_); }

void CheckedMutex::CheckNoLocksImpl() { deadlock_detector.CheckNoLocks(); }
#endif

}  // namespace __sanitizer
