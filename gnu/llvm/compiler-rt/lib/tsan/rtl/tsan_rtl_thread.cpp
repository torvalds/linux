//===-- tsan_rtl_thread.cpp -----------------------------------------------===//
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

#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_platform.h"
#include "tsan_report.h"
#include "tsan_sync.h"

namespace __tsan {

// ThreadContext implementation.

ThreadContext::ThreadContext(Tid tid) : ThreadContextBase(tid), thr(), sync() {}

#if !SANITIZER_GO
ThreadContext::~ThreadContext() {
}
#endif

void ThreadContext::OnReset() { CHECK(!sync); }

#if !SANITIZER_GO
struct ThreadLeak {
  ThreadContext *tctx;
  int count;
};

static void CollectThreadLeaks(ThreadContextBase *tctx_base, void *arg) {
  auto &leaks = *static_cast<Vector<ThreadLeak> *>(arg);
  auto *tctx = static_cast<ThreadContext *>(tctx_base);
  if (tctx->detached || tctx->status != ThreadStatusFinished)
    return;
  for (uptr i = 0; i < leaks.Size(); i++) {
    if (leaks[i].tctx->creation_stack_id == tctx->creation_stack_id) {
      leaks[i].count++;
      return;
    }
  }
  leaks.PushBack({tctx, 1});
}
#endif

// Disabled on Mac because lldb test TestTsanBasic fails:
// https://reviews.llvm.org/D112603#3163158
#if !SANITIZER_GO && !SANITIZER_APPLE
static void ReportIgnoresEnabled(ThreadContext *tctx, IgnoreSet *set) {
  if (tctx->tid == kMainTid) {
    Printf("ThreadSanitizer: main thread finished with ignores enabled\n");
  } else {
    Printf("ThreadSanitizer: thread T%d %s finished with ignores enabled,"
      " created at:\n", tctx->tid, tctx->name);
    PrintStack(SymbolizeStackId(tctx->creation_stack_id));
  }
  Printf("  One of the following ignores was not ended"
      " (in order of probability)\n");
  for (uptr i = 0; i < set->Size(); i++) {
    Printf("  Ignore was enabled at:\n");
    PrintStack(SymbolizeStackId(set->At(i)));
  }
  Die();
}

static void ThreadCheckIgnore(ThreadState *thr) {
  if (ctx->after_multithreaded_fork)
    return;
  if (thr->ignore_reads_and_writes)
    ReportIgnoresEnabled(thr->tctx, &thr->mop_ignore_set);
  if (thr->ignore_sync)
    ReportIgnoresEnabled(thr->tctx, &thr->sync_ignore_set);
}
#else
static void ThreadCheckIgnore(ThreadState *thr) {}
#endif

void ThreadFinalize(ThreadState *thr) {
  ThreadCheckIgnore(thr);
#if !SANITIZER_GO
  if (!ShouldReport(thr, ReportTypeThreadLeak))
    return;
  ThreadRegistryLock l(&ctx->thread_registry);
  Vector<ThreadLeak> leaks;
  ctx->thread_registry.RunCallbackForEachThreadLocked(CollectThreadLeaks,
                                                      &leaks);
  for (uptr i = 0; i < leaks.Size(); i++) {
    ScopedReport rep(ReportTypeThreadLeak);
    rep.AddThread(leaks[i].tctx, true);
    rep.SetCount(leaks[i].count);
    OutputReport(thr, rep);
  }
#endif
}

int ThreadCount(ThreadState *thr) {
  uptr result;
  ctx->thread_registry.GetNumberOfThreads(0, 0, &result);
  return (int)result;
}

struct OnCreatedArgs {
  VectorClock *sync;
  uptr sync_epoch;
  StackID stack;
};

Tid ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached) {
  // The main thread and GCD workers don't have a parent thread.
  Tid parent = kInvalidTid;
  OnCreatedArgs arg = {nullptr, 0, kInvalidStackID};
  if (thr) {
    parent = thr->tid;
    arg.stack = CurrentStackId(thr, pc);
    if (!thr->ignore_sync) {
      SlotLocker locker(thr);
      thr->clock.ReleaseStore(&arg.sync);
      arg.sync_epoch = ctx->global_epoch;
      IncrementEpoch(thr);
    }
  }
  Tid tid = ctx->thread_registry.CreateThread(uid, detached, parent, &arg);
  DPrintf("#%d: ThreadCreate tid=%d uid=%zu\n", parent, tid, uid);
  return tid;
}

void ThreadContext::OnCreated(void *arg) {
  OnCreatedArgs *args = static_cast<OnCreatedArgs *>(arg);
  sync = args->sync;
  sync_epoch = args->sync_epoch;
  creation_stack_id = args->stack;
}

extern "C" void __tsan_stack_initialization() {}

struct OnStartedArgs {
  ThreadState *thr;
  uptr stk_addr;
  uptr stk_size;
  uptr tls_addr;
  uptr tls_size;
};

void ThreadStart(ThreadState *thr, Tid tid, tid_t os_id,
                 ThreadType thread_type) {
  ctx->thread_registry.StartThread(tid, os_id, thread_type, thr);
  if (!thr->ignore_sync) {
    SlotAttachAndLock(thr);
    if (thr->tctx->sync_epoch == ctx->global_epoch)
      thr->clock.Acquire(thr->tctx->sync);
    SlotUnlock(thr);
  }
  Free(thr->tctx->sync);

#if !SANITIZER_GO
  thr->is_inited = true;
#endif

  uptr stk_addr = 0;
  uptr stk_size = 0;
  uptr tls_addr = 0;
  uptr tls_size = 0;
#if !SANITIZER_GO
  if (thread_type != ThreadType::Fiber)
    GetThreadStackAndTls(tid == kMainTid, &stk_addr, &stk_size, &tls_addr,
                         &tls_size);
#endif
  thr->stk_addr = stk_addr;
  thr->stk_size = stk_size;
  thr->tls_addr = tls_addr;
  thr->tls_size = tls_size;

#if !SANITIZER_GO
  if (ctx->after_multithreaded_fork) {
    thr->ignore_interceptors++;
    ThreadIgnoreBegin(thr, 0);
    ThreadIgnoreSyncBegin(thr, 0);
  }
#endif

#if !SANITIZER_GO
  // Don't imitate stack/TLS writes for the main thread,
  // because its initialization is synchronized with all
  // subsequent threads anyway.
  if (tid != kMainTid) {
    if (stk_addr && stk_size) {
      const uptr pc = StackTrace::GetNextInstructionPc(
          reinterpret_cast<uptr>(__tsan_stack_initialization));
      MemoryRangeImitateWrite(thr, pc, stk_addr, stk_size);
    }

    if (tls_addr && tls_size)
      ImitateTlsWrite(thr, tls_addr, tls_size);
  }
#endif
}

void ThreadContext::OnStarted(void *arg) {
  DPrintf("#%d: ThreadStart\n", tid);
  thr = new (arg) ThreadState(tid);
  if (common_flags()->detect_deadlocks)
    thr->dd_lt = ctx->dd->CreateLogicalThread(tid);
  thr->tctx = this;
}

void ThreadFinish(ThreadState *thr) {
  DPrintf("#%d: ThreadFinish\n", thr->tid);
  ThreadCheckIgnore(thr);
  if (thr->stk_addr && thr->stk_size)
    DontNeedShadowFor(thr->stk_addr, thr->stk_size);
  if (thr->tls_addr && thr->tls_size)
    DontNeedShadowFor(thr->tls_addr, thr->tls_size);
  thr->is_dead = true;
#if !SANITIZER_GO
  thr->is_inited = false;
  thr->ignore_interceptors++;
  PlatformCleanUpThreadState(thr);
#endif
  if (!thr->ignore_sync) {
    SlotLocker locker(thr);
    ThreadRegistryLock lock(&ctx->thread_registry);
    // Note: detached is protected by the thread registry mutex,
    // the thread may be detaching concurrently in another thread.
    if (!thr->tctx->detached) {
      thr->clock.ReleaseStore(&thr->tctx->sync);
      thr->tctx->sync_epoch = ctx->global_epoch;
      IncrementEpoch(thr);
    }
  }
#if !SANITIZER_GO
  UnmapOrDie(thr->shadow_stack, kShadowStackSize * sizeof(uptr));
#else
  Free(thr->shadow_stack);
#endif
  thr->shadow_stack = nullptr;
  thr->shadow_stack_pos = nullptr;
  thr->shadow_stack_end = nullptr;
  if (common_flags()->detect_deadlocks)
    ctx->dd->DestroyLogicalThread(thr->dd_lt);
  SlotDetach(thr);
  ctx->thread_registry.FinishThread(thr->tid);
  thr->~ThreadState();
}

void ThreadContext::OnFinished() {
  Lock lock(&ctx->slot_mtx);
  Lock lock1(&trace.mtx);
  // Queue all trace parts into the global recycle queue.
  auto parts = &trace.parts;
  while (trace.local_head) {
    CHECK(parts->Queued(trace.local_head));
    ctx->trace_part_recycle.PushBack(trace.local_head);
    trace.local_head = parts->Next(trace.local_head);
  }
  ctx->trace_part_recycle_finished += parts->Size();
  if (ctx->trace_part_recycle_finished > Trace::kFinishedThreadHi) {
    ctx->trace_part_finished_excess += parts->Size();
    trace.parts_allocated = 0;
  } else if (ctx->trace_part_recycle_finished > Trace::kFinishedThreadLo &&
             parts->Size() > 1) {
    ctx->trace_part_finished_excess += parts->Size() - 1;
    trace.parts_allocated = 1;
  }
  // From now on replay will use trace->final_pos.
  trace.final_pos = (Event *)atomic_load_relaxed(&thr->trace_pos);
  atomic_store_relaxed(&thr->trace_pos, 0);
  thr->tctx = nullptr;
  thr = nullptr;
}

struct ConsumeThreadContext {
  uptr uid;
  ThreadContextBase *tctx;
};

Tid ThreadConsumeTid(ThreadState *thr, uptr pc, uptr uid) {
  return ctx->thread_registry.ConsumeThreadUserId(uid);
}

struct JoinArg {
  VectorClock *sync;
  uptr sync_epoch;
};

void ThreadJoin(ThreadState *thr, uptr pc, Tid tid) {
  CHECK_GT(tid, 0);
  DPrintf("#%d: ThreadJoin tid=%d\n", thr->tid, tid);
  JoinArg arg = {};
  ctx->thread_registry.JoinThread(tid, &arg);
  if (!thr->ignore_sync) {
    SlotLocker locker(thr);
    if (arg.sync_epoch == ctx->global_epoch)
      thr->clock.Acquire(arg.sync);
  }
  Free(arg.sync);
}

void ThreadContext::OnJoined(void *ptr) {
  auto arg = static_cast<JoinArg *>(ptr);
  arg->sync = sync;
  arg->sync_epoch = sync_epoch;
  sync = nullptr;
  sync_epoch = 0;
}

void ThreadContext::OnDead() { CHECK_EQ(sync, nullptr); }

void ThreadDetach(ThreadState *thr, uptr pc, Tid tid) {
  CHECK_GT(tid, 0);
  ctx->thread_registry.DetachThread(tid, thr);
}

void ThreadContext::OnDetached(void *arg) { Free(sync); }

void ThreadNotJoined(ThreadState *thr, uptr pc, Tid tid, uptr uid) {
  CHECK_GT(tid, 0);
  ctx->thread_registry.SetThreadUserId(tid, uid);
}

void ThreadSetName(ThreadState *thr, const char *name) {
  ctx->thread_registry.SetThreadName(thr->tid, name);
}

#if !SANITIZER_GO
void FiberSwitchImpl(ThreadState *from, ThreadState *to) {
  Processor *proc = from->proc();
  ProcUnwire(proc, from);
  ProcWire(proc, to);
  set_cur_thread(to);
}

ThreadState *FiberCreate(ThreadState *thr, uptr pc, unsigned flags) {
  void *mem = Alloc(sizeof(ThreadState));
  ThreadState *fiber = static_cast<ThreadState *>(mem);
  internal_memset(fiber, 0, sizeof(*fiber));
  Tid tid = ThreadCreate(thr, pc, 0, true);
  FiberSwitchImpl(thr, fiber);
  ThreadStart(fiber, tid, 0, ThreadType::Fiber);
  FiberSwitchImpl(fiber, thr);
  return fiber;
}

void FiberDestroy(ThreadState *thr, uptr pc, ThreadState *fiber) {
  FiberSwitchImpl(thr, fiber);
  ThreadFinish(fiber);
  FiberSwitchImpl(fiber, thr);
  Free(fiber);
}

void FiberSwitch(ThreadState *thr, uptr pc,
                 ThreadState *fiber, unsigned flags) {
  if (!(flags & FiberSwitchFlagNoSync))
    Release(thr, pc, (uptr)fiber);
  FiberSwitchImpl(thr, fiber);
  if (!(flags & FiberSwitchFlagNoSync))
    Acquire(fiber, pc, (uptr)fiber);
}
#endif

}  // namespace __tsan
