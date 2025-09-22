//===-- sanitizer_deadlock_detector1.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Deadlock detector implementation based on NxN adjacency bit matrix.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_deadlock_detector_interface.h"
#include "sanitizer_deadlock_detector.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_mutex.h"

#if SANITIZER_DEADLOCK_DETECTOR_VERSION == 1

namespace __sanitizer {

typedef TwoLevelBitVector<> DDBV;  // DeadlockDetector's bit vector.

struct DDPhysicalThread {
};

struct DDLogicalThread {
  u64 ctx;
  DeadlockDetectorTLS<DDBV> dd;
  DDReport rep;
  bool report_pending;
};

struct DD final : public DDetector {
  SpinMutex mtx;
  DeadlockDetector<DDBV> dd;
  DDFlags flags;

  explicit DD(const DDFlags *flags);

  DDPhysicalThread *CreatePhysicalThread() override;
  void DestroyPhysicalThread(DDPhysicalThread *pt) override;

  DDLogicalThread *CreateLogicalThread(u64 ctx) override;
  void DestroyLogicalThread(DDLogicalThread *lt) override;

  void MutexInit(DDCallback *cb, DDMutex *m) override;
  void MutexBeforeLock(DDCallback *cb, DDMutex *m, bool wlock) override;
  void MutexAfterLock(DDCallback *cb, DDMutex *m, bool wlock,
                      bool trylock) override;
  void MutexBeforeUnlock(DDCallback *cb, DDMutex *m, bool wlock) override;
  void MutexDestroy(DDCallback *cb, DDMutex *m) override;

  DDReport *GetReport(DDCallback *cb) override;

  void MutexEnsureID(DDLogicalThread *lt, DDMutex *m);
  void ReportDeadlock(DDCallback *cb, DDMutex *m);
};

DDetector *DDetector::Create(const DDFlags *flags) {
  (void)flags;
  void *mem = MmapOrDie(sizeof(DD), "deadlock detector");
  return new(mem) DD(flags);
}

DD::DD(const DDFlags *flags)
    : flags(*flags) {
  dd.clear();
}

DDPhysicalThread* DD::CreatePhysicalThread() {
  return nullptr;
}

void DD::DestroyPhysicalThread(DDPhysicalThread *pt) {
}

DDLogicalThread* DD::CreateLogicalThread(u64 ctx) {
  DDLogicalThread *lt = (DDLogicalThread*)InternalAlloc(sizeof(*lt));
  lt->ctx = ctx;
  lt->dd.clear();
  lt->report_pending = false;
  return lt;
}

void DD::DestroyLogicalThread(DDLogicalThread *lt) {
  lt->~DDLogicalThread();
  InternalFree(lt);
}

void DD::MutexInit(DDCallback *cb, DDMutex *m) {
  m->id = 0;
  m->stk = cb->Unwind();
}

void DD::MutexEnsureID(DDLogicalThread *lt, DDMutex *m) {
  if (!dd.nodeBelongsToCurrentEpoch(m->id))
    m->id = dd.newNode(reinterpret_cast<uptr>(m));
  dd.ensureCurrentEpoch(&lt->dd);
}

void DD::MutexBeforeLock(DDCallback *cb,
    DDMutex *m, bool wlock) {
  DDLogicalThread *lt = cb->lt;
  if (lt->dd.empty()) return;  // This will be the first lock held by lt.
  if (dd.hasAllEdges(&lt->dd, m->id)) return;  // We already have all edges.
  SpinMutexLock lk(&mtx);
  MutexEnsureID(lt, m);
  if (dd.isHeld(&lt->dd, m->id))
    return;  // FIXME: allow this only for recursive locks.
  if (dd.onLockBefore(&lt->dd, m->id)) {
    // Actually add this edge now so that we have all the stack traces.
    dd.addEdges(&lt->dd, m->id, cb->Unwind(), cb->UniqueTid());
    ReportDeadlock(cb, m);
  }
}

void DD::ReportDeadlock(DDCallback *cb, DDMutex *m) {
  DDLogicalThread *lt = cb->lt;
  uptr path[20];
  uptr len = dd.findPathToLock(&lt->dd, m->id, path, ARRAY_SIZE(path));
  if (len == 0U) {
    // A cycle of 20+ locks? Well, that's a bit odd...
    Printf("WARNING: too long mutex cycle found\n");
    return;
  }
  CHECK_EQ(m->id, path[0]);
  lt->report_pending = true;
  len = Min<uptr>(len, DDReport::kMaxLoopSize);
  DDReport *rep = &lt->rep;
  rep->n = len;
  for (uptr i = 0; i < len; i++) {
    uptr from = path[i];
    uptr to = path[(i + 1) % len];
    DDMutex *m0 = (DDMutex*)dd.getData(from);
    DDMutex *m1 = (DDMutex*)dd.getData(to);

    u32 stk_from = 0, stk_to = 0;
    int unique_tid = 0;
    dd.findEdge(from, to, &stk_from, &stk_to, &unique_tid);
    // Printf("Edge: %zd=>%zd: %u/%u T%d\n", from, to, stk_from, stk_to,
    //    unique_tid);
    rep->loop[i].thr_ctx = unique_tid;
    rep->loop[i].mtx_ctx0 = m0->ctx;
    rep->loop[i].mtx_ctx1 = m1->ctx;
    rep->loop[i].stk[0] = stk_to;
    rep->loop[i].stk[1] = stk_from;
  }
}

void DD::MutexAfterLock(DDCallback *cb, DDMutex *m, bool wlock, bool trylock) {
  DDLogicalThread *lt = cb->lt;
  u32 stk = 0;
  if (flags.second_deadlock_stack)
    stk = cb->Unwind();
  // Printf("T%p MutexLock:   %zx stk %u\n", lt, m->id, stk);
  if (dd.onFirstLock(&lt->dd, m->id, stk))
    return;
  if (dd.onLockFast(&lt->dd, m->id, stk))
    return;

  SpinMutexLock lk(&mtx);
  MutexEnsureID(lt, m);
  if (wlock)  // Only a recursive rlock may be held.
    CHECK(!dd.isHeld(&lt->dd, m->id));
  if (!trylock)
    dd.addEdges(&lt->dd, m->id, stk ? stk : cb->Unwind(), cb->UniqueTid());
  dd.onLockAfter(&lt->dd, m->id, stk);
}

void DD::MutexBeforeUnlock(DDCallback *cb, DDMutex *m, bool wlock) {
  // Printf("T%p MutexUnLock: %zx\n", cb->lt, m->id);
  dd.onUnlock(&cb->lt->dd, m->id);
}

void DD::MutexDestroy(DDCallback *cb,
    DDMutex *m) {
  if (!m->id) return;
  SpinMutexLock lk(&mtx);
  if (dd.nodeBelongsToCurrentEpoch(m->id))
    dd.removeNode(m->id);
  m->id = 0;
}

DDReport *DD::GetReport(DDCallback *cb) {
  if (!cb->lt->report_pending)
    return nullptr;
  cb->lt->report_pending = false;
  return &cb->lt->rep;
}

} // namespace __sanitizer
#endif // #if SANITIZER_DEADLOCK_DETECTOR_VERSION == 1
