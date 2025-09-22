//===-- sanitizer_deadlock_detector2.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Deadlock detector implementation based on adjacency lists.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_deadlock_detector_interface.h"
#include "sanitizer_common.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_mutex.h"

#if SANITIZER_DEADLOCK_DETECTOR_VERSION == 2

namespace __sanitizer {

const int kMaxNesting = 64;
const u32 kNoId = -1;
const u32 kEndId = -2;
const int kMaxLink = 8;
const int kL1Size = 1024;
const int kL2Size = 1024;
const int kMaxMutex = kL1Size * kL2Size;

struct Id {
  u32 id;
  u32 seq;

  explicit Id(u32 id = 0, u32 seq = 0)
      : id(id)
      , seq(seq) {
  }
};

struct Link {
  u32 id;
  u32 seq;
  u32 tid;
  u32 stk0;
  u32 stk1;

  explicit Link(u32 id = 0, u32 seq = 0, u32 tid = 0, u32 s0 = 0, u32 s1 = 0)
      : id(id)
      , seq(seq)
      , tid(tid)
      , stk0(s0)
      , stk1(s1) {
  }
};

struct DDPhysicalThread {
  DDReport rep;
  bool report_pending;
  bool visited[kMaxMutex];
  Link pending[kMaxMutex];
  Link path[kMaxMutex];
};

struct ThreadMutex {
  u32 id;
  u32 stk;
};

struct DDLogicalThread {
  u64         ctx;
  ThreadMutex locked[kMaxNesting];
  int         nlocked;
};

struct MutexState {
  StaticSpinMutex mtx;
  u32 seq;
  int nlink;
  Link link[kMaxLink];
};

struct DD final : public DDetector {
  explicit DD(const DDFlags *flags);

  DDPhysicalThread* CreatePhysicalThread();
  void DestroyPhysicalThread(DDPhysicalThread *pt);

  DDLogicalThread* CreateLogicalThread(u64 ctx);
  void DestroyLogicalThread(DDLogicalThread *lt);

  void MutexInit(DDCallback *cb, DDMutex *m);
  void MutexBeforeLock(DDCallback *cb, DDMutex *m, bool wlock);
  void MutexAfterLock(DDCallback *cb, DDMutex *m, bool wlock,
      bool trylock);
  void MutexBeforeUnlock(DDCallback *cb, DDMutex *m, bool wlock);
  void MutexDestroy(DDCallback *cb, DDMutex *m);

  DDReport *GetReport(DDCallback *cb);

  void CycleCheck(DDPhysicalThread *pt, DDLogicalThread *lt, DDMutex *mtx);
  void Report(DDPhysicalThread *pt, DDLogicalThread *lt, int npath);
  u32 allocateId(DDCallback *cb);
  MutexState *getMutex(u32 id);
  u32 getMutexId(MutexState *m);

  DDFlags flags;

  MutexState *mutex[kL1Size];

  SpinMutex mtx;
  InternalMmapVector<u32> free_id;
  int id_gen = 0;
};

DDetector *DDetector::Create(const DDFlags *flags) {
  (void)flags;
  void *mem = MmapOrDie(sizeof(DD), "deadlock detector");
  return new(mem) DD(flags);
}

DD::DD(const DDFlags *flags) : flags(*flags) { free_id.reserve(1024); }

DDPhysicalThread* DD::CreatePhysicalThread() {
  DDPhysicalThread *pt = (DDPhysicalThread*)MmapOrDie(sizeof(DDPhysicalThread),
      "deadlock detector (physical thread)");
  return pt;
}

void DD::DestroyPhysicalThread(DDPhysicalThread *pt) {
  pt->~DDPhysicalThread();
  UnmapOrDie(pt, sizeof(DDPhysicalThread));
}

DDLogicalThread* DD::CreateLogicalThread(u64 ctx) {
  DDLogicalThread *lt = (DDLogicalThread*)InternalAlloc(
      sizeof(DDLogicalThread));
  lt->ctx = ctx;
  lt->nlocked = 0;
  return lt;
}

void DD::DestroyLogicalThread(DDLogicalThread *lt) {
  lt->~DDLogicalThread();
  InternalFree(lt);
}

void DD::MutexInit(DDCallback *cb, DDMutex *m) {
  VPrintf(2, "#%llu: DD::MutexInit(%p)\n", cb->lt->ctx, m);
  m->id = kNoId;
  m->recursion = 0;
  atomic_store(&m->owner, 0, memory_order_relaxed);
}

MutexState *DD::getMutex(u32 id) { return &mutex[id / kL2Size][id % kL2Size]; }

u32 DD::getMutexId(MutexState *m) {
  for (int i = 0; i < kL1Size; i++) {
    MutexState *tab = mutex[i];
    if (tab == 0)
      break;
    if (m >= tab && m < tab + kL2Size)
      return i * kL2Size + (m - tab);
  }
  return -1;
}

u32 DD::allocateId(DDCallback *cb) {
  u32 id = -1;
  SpinMutexLock l(&mtx);
  if (free_id.size() > 0) {
    id = free_id.back();
    free_id.pop_back();
  } else {
    CHECK_LT(id_gen, kMaxMutex);
    if ((id_gen % kL2Size) == 0) {
      mutex[id_gen / kL2Size] = (MutexState *)MmapOrDie(
          kL2Size * sizeof(MutexState), "deadlock detector (mutex table)");
    }
    id = id_gen++;
  }
  CHECK_LE(id, kMaxMutex);
  VPrintf(3, "#%llu: DD::allocateId assign id %d\n", cb->lt->ctx, id);
  return id;
}

void DD::MutexBeforeLock(DDCallback *cb, DDMutex *m, bool wlock) {
  VPrintf(2, "#%llu: DD::MutexBeforeLock(%p, wlock=%d) nlocked=%d\n",
      cb->lt->ctx, m, wlock, cb->lt->nlocked);
  DDPhysicalThread *pt = cb->pt;
  DDLogicalThread *lt = cb->lt;

  uptr owner = atomic_load(&m->owner, memory_order_relaxed);
  if (owner == (uptr)cb->lt) {
    VPrintf(3, "#%llu: DD::MutexBeforeLock recursive\n",
        cb->lt->ctx);
    return;
  }

  CHECK_LE(lt->nlocked, kMaxNesting);

  // FIXME(dvyukov): don't allocate id if lt->nlocked == 0?
  if (m->id == kNoId)
    m->id = allocateId(cb);

  ThreadMutex *tm = &lt->locked[lt->nlocked++];
  tm->id = m->id;
  if (flags.second_deadlock_stack)
    tm->stk = cb->Unwind();
  if (lt->nlocked == 1) {
    VPrintf(3, "#%llu: DD::MutexBeforeLock first mutex\n",
        cb->lt->ctx);
    return;
  }

  bool added = false;
  MutexState *mtx = getMutex(m->id);
  for (int i = 0; i < lt->nlocked - 1; i++) {
    u32 id1 = lt->locked[i].id;
    u32 stk1 = lt->locked[i].stk;
    MutexState *mtx1 = getMutex(id1);
    SpinMutexLock l(&mtx1->mtx);
    if (mtx1->nlink == kMaxLink) {
      // FIXME(dvyukov): check stale links
      continue;
    }
    int li = 0;
    for (; li < mtx1->nlink; li++) {
      Link *link = &mtx1->link[li];
      if (link->id == m->id) {
        if (link->seq != mtx->seq) {
          link->seq = mtx->seq;
          link->tid = lt->ctx;
          link->stk0 = stk1;
          link->stk1 = cb->Unwind();
          added = true;
          VPrintf(3, "#%llu: DD::MutexBeforeLock added %d->%d link\n",
              cb->lt->ctx, getMutexId(mtx1), m->id);
        }
        break;
      }
    }
    if (li == mtx1->nlink) {
      // FIXME(dvyukov): check stale links
      Link *link = &mtx1->link[mtx1->nlink++];
      link->id = m->id;
      link->seq = mtx->seq;
      link->tid = lt->ctx;
      link->stk0 = stk1;
      link->stk1 = cb->Unwind();
      added = true;
      VPrintf(3, "#%llu: DD::MutexBeforeLock added %d->%d link\n",
          cb->lt->ctx, getMutexId(mtx1), m->id);
    }
  }

  if (!added || mtx->nlink == 0) {
    VPrintf(3, "#%llu: DD::MutexBeforeLock don't check\n",
        cb->lt->ctx);
    return;
  }

  CycleCheck(pt, lt, m);
}

void DD::MutexAfterLock(DDCallback *cb, DDMutex *m, bool wlock,
    bool trylock) {
  VPrintf(2, "#%llu: DD::MutexAfterLock(%p, wlock=%d, try=%d) nlocked=%d\n",
      cb->lt->ctx, m, wlock, trylock, cb->lt->nlocked);
  DDLogicalThread *lt = cb->lt;

  uptr owner = atomic_load(&m->owner, memory_order_relaxed);
  if (owner == (uptr)cb->lt) {
    VPrintf(3, "#%llu: DD::MutexAfterLock recursive\n", cb->lt->ctx);
    CHECK(wlock);
    m->recursion++;
    return;
  }
  CHECK_EQ(owner, 0);
  if (wlock) {
    VPrintf(3, "#%llu: DD::MutexAfterLock set owner\n", cb->lt->ctx);
    CHECK_EQ(m->recursion, 0);
    m->recursion = 1;
    atomic_store(&m->owner, (uptr)cb->lt, memory_order_relaxed);
  }

  if (!trylock)
    return;

  CHECK_LE(lt->nlocked, kMaxNesting);
  if (m->id == kNoId)
    m->id = allocateId(cb);
  ThreadMutex *tm = &lt->locked[lt->nlocked++];
  tm->id = m->id;
  if (flags.second_deadlock_stack)
    tm->stk = cb->Unwind();
}

void DD::MutexBeforeUnlock(DDCallback *cb, DDMutex *m, bool wlock) {
  VPrintf(2, "#%llu: DD::MutexBeforeUnlock(%p, wlock=%d) nlocked=%d\n",
      cb->lt->ctx, m, wlock, cb->lt->nlocked);
  DDLogicalThread *lt = cb->lt;

  uptr owner = atomic_load(&m->owner, memory_order_relaxed);
  if (owner == (uptr)cb->lt) {
    VPrintf(3, "#%llu: DD::MutexBeforeUnlock recursive\n", cb->lt->ctx);
    if (--m->recursion > 0)
      return;
    VPrintf(3, "#%llu: DD::MutexBeforeUnlock reset owner\n", cb->lt->ctx);
    atomic_store(&m->owner, 0, memory_order_relaxed);
  }
  CHECK_NE(m->id, kNoId);
  int last = lt->nlocked - 1;
  for (int i = last; i >= 0; i--) {
    if (cb->lt->locked[i].id == m->id) {
      lt->locked[i] = lt->locked[last];
      lt->nlocked--;
      break;
    }
  }
}

void DD::MutexDestroy(DDCallback *cb, DDMutex *m) {
  VPrintf(2, "#%llu: DD::MutexDestroy(%p)\n",
      cb->lt->ctx, m);
  DDLogicalThread *lt = cb->lt;

  if (m->id == kNoId)
    return;

  // Remove the mutex from lt->locked if there.
  int last = lt->nlocked - 1;
  for (int i = last; i >= 0; i--) {
    if (lt->locked[i].id == m->id) {
      lt->locked[i] = lt->locked[last];
      lt->nlocked--;
      break;
    }
  }

  // Clear and invalidate the mutex descriptor.
  {
    MutexState *mtx = getMutex(m->id);
    SpinMutexLock l(&mtx->mtx);
    mtx->seq++;
    mtx->nlink = 0;
  }

  // Return id to cache.
  {
    SpinMutexLock l(&mtx);
    free_id.push_back(m->id);
  }
}

void DD::CycleCheck(DDPhysicalThread *pt, DDLogicalThread *lt,
    DDMutex *m) {
  internal_memset(pt->visited, 0, sizeof(pt->visited));
  int npath = 0;
  int npending = 0;
  {
    MutexState *mtx = getMutex(m->id);
    SpinMutexLock l(&mtx->mtx);
    for (int li = 0; li < mtx->nlink; li++)
      pt->pending[npending++] = mtx->link[li];
  }
  while (npending > 0) {
    Link link = pt->pending[--npending];
    if (link.id == kEndId) {
      npath--;
      continue;
    }
    if (pt->visited[link.id])
      continue;
    MutexState *mtx1 = getMutex(link.id);
    SpinMutexLock l(&mtx1->mtx);
    if (mtx1->seq != link.seq)
      continue;
    pt->visited[link.id] = true;
    if (mtx1->nlink == 0)
      continue;
    pt->path[npath++] = link;
    pt->pending[npending++] = Link(kEndId);
    if (link.id == m->id)
      return Report(pt, lt, npath);  // Bingo!
    for (int li = 0; li < mtx1->nlink; li++) {
      Link *link1 = &mtx1->link[li];
      // MutexState *mtx2 = getMutex(link->id);
      // FIXME(dvyukov): fast seq check
      // FIXME(dvyukov): fast nlink != 0 check
      // FIXME(dvyukov): fast pending check?
      // FIXME(dvyukov): npending can be larger than kMaxMutex
      pt->pending[npending++] = *link1;
    }
  }
}

void DD::Report(DDPhysicalThread *pt, DDLogicalThread *lt, int npath) {
  DDReport *rep = &pt->rep;
  rep->n = npath;
  for (int i = 0; i < npath; i++) {
    Link *link = &pt->path[i];
    Link *link0 = &pt->path[i ? i - 1 : npath - 1];
    rep->loop[i].thr_ctx = link->tid;
    rep->loop[i].mtx_ctx0 = link0->id;
    rep->loop[i].mtx_ctx1 = link->id;
    rep->loop[i].stk[0] = flags.second_deadlock_stack ? link->stk0 : 0;
    rep->loop[i].stk[1] = link->stk1;
  }
  pt->report_pending = true;
}

DDReport *DD::GetReport(DDCallback *cb) {
  if (!cb->pt->report_pending)
    return 0;
  cb->pt->report_pending = false;
  return &cb->pt->rep;
}

}  // namespace __sanitizer
#endif  // #if SANITIZER_DEADLOCK_DETECTOR_VERSION == 2
