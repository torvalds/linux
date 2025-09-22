//===-- tsan_trace_test.cpp -----------------------------------------------===//
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
#include "tsan_trace.h"

#include <pthread.h>

#include "gtest/gtest.h"
#include "tsan_rtl.h"

#if !defined(__x86_64__)
// These tests are currently crashing on ppc64:
// https://reviews.llvm.org/D110546#3025422
// due to the way we create thread contexts
// There must be some difference in thread initialization
// between normal execution and unit tests.
#  define TRACE_TEST(SUITE, NAME) TEST(SUITE, DISABLED_##NAME)
#else
#  define TRACE_TEST(SUITE, NAME) TEST(SUITE, NAME)
#endif

namespace __tsan {

// We need to run all trace tests in a new thread,
// so that the thread trace is empty initially.
template <uptr N>
struct ThreadArray {
  ThreadArray() {
    for (auto *&thr : threads) {
      thr = static_cast<ThreadState *>(
          MmapOrDie(sizeof(ThreadState), "ThreadState"));
      Tid tid = ThreadCreate(cur_thread(), 0, 0, true);
      Processor *proc = ProcCreate();
      ProcWire(proc, thr);
      ThreadStart(thr, tid, 0, ThreadType::Fiber);
    }
  }

  ~ThreadArray() {
    for (uptr i = 0; i < N; i++) {
      if (threads[i])
        Finish(i);
    }
  }

  void Finish(uptr i) {
    auto *thr = threads[i];
    threads[i] = nullptr;
    Processor *proc = thr->proc();
    ThreadFinish(thr);
    ProcUnwire(proc, thr);
    ProcDestroy(proc);
    UnmapOrDie(thr, sizeof(ThreadState));
  }

  ThreadState *threads[N];
  ThreadState *operator[](uptr i) { return threads[i]; }
  ThreadState *operator->() { return threads[0]; }
  operator ThreadState *() { return threads[0]; }
};

TRACE_TEST(Trace, RestoreAccess) {
  // A basic test with some function entry/exit events,
  // some mutex lock/unlock events and some other distracting
  // memory events.
  ThreadArray<1> thr;
  TraceFunc(thr, 0x1000);
  TraceFunc(thr, 0x1001);
  TraceMutexLock(thr, EventType::kLock, 0x4000, 0x5000, 0x6000);
  TraceMutexLock(thr, EventType::kLock, 0x4001, 0x5001, 0x6001);
  TraceMutexUnlock(thr, 0x5000);
  TraceFunc(thr);
  CHECK(TryTraceMemoryAccess(thr, 0x2001, 0x3001, 8, kAccessRead));
  TraceMutexLock(thr, EventType::kRLock, 0x4002, 0x5002, 0x6002);
  TraceFunc(thr, 0x1002);
  CHECK(TryTraceMemoryAccess(thr, 0x2000, 0x3000, 8, kAccessRead));
  // This is the access we want to find.
  // The previous one is equivalent, but RestoreStack must prefer
  // the last of the matchig accesses.
  CHECK(TryTraceMemoryAccess(thr, 0x2002, 0x3000, 8, kAccessRead));
  Lock slot_lock(&ctx->slots[static_cast<uptr>(thr->fast_state.sid())].mtx);
  ThreadRegistryLock lock1(&ctx->thread_registry);
  Lock lock2(&ctx->slot_mtx);
  Tid tid = kInvalidTid;
  VarSizeStackTrace stk;
  MutexSet mset;
  uptr tag = kExternalTagNone;
  bool res = RestoreStack(EventType::kAccessExt, thr->fast_state.sid(),
                          thr->fast_state.epoch(), 0x3000, 8, kAccessRead, &tid,
                          &stk, &mset, &tag);
  CHECK(res);
  CHECK_EQ(tid, thr->tid);
  CHECK_EQ(stk.size, 3);
  CHECK_EQ(stk.trace[0], 0x1000);
  CHECK_EQ(stk.trace[1], 0x1002);
  CHECK_EQ(stk.trace[2], 0x2002);
  CHECK_EQ(mset.Size(), 2);
  CHECK_EQ(mset.Get(0).addr, 0x5001);
  CHECK_EQ(mset.Get(0).stack_id, 0x6001);
  CHECK_EQ(mset.Get(0).write, true);
  CHECK_EQ(mset.Get(1).addr, 0x5002);
  CHECK_EQ(mset.Get(1).stack_id, 0x6002);
  CHECK_EQ(mset.Get(1).write, false);
  CHECK_EQ(tag, kExternalTagNone);
}

TRACE_TEST(Trace, MemoryAccessSize) {
  // Test tracing and matching of accesses of different sizes.
  struct Params {
    uptr access_size, offset, size;
    bool res;
  };
  Params tests[] = {
      {1, 0, 1, true},  {4, 0, 2, true},
      {4, 2, 2, true},  {8, 3, 1, true},
      {2, 1, 1, true},  {1, 1, 1, false},
      {8, 5, 4, false}, {4, static_cast<uptr>(-1l), 4, false},
  };
  for (auto params : tests) {
    for (int type = 0; type < 3; type++) {
      ThreadArray<1> thr;
      Printf("access_size=%zu, offset=%zu, size=%zu, res=%d, type=%d\n",
             params.access_size, params.offset, params.size, params.res, type);
      TraceFunc(thr, 0x1000);
      switch (type) {
        case 0:
          // This should emit compressed event.
          CHECK(TryTraceMemoryAccess(thr, 0x2000, 0x3000, params.access_size,
                                     kAccessRead));
          break;
        case 1:
          // This should emit full event.
          CHECK(TryTraceMemoryAccess(thr, 0x2000000, 0x3000, params.access_size,
                                     kAccessRead));
          break;
        case 2:
          TraceMemoryAccessRange(thr, 0x2000000, 0x3000, params.access_size,
                                 kAccessRead);
          break;
      }
      Lock slot_lock(&ctx->slots[static_cast<uptr>(thr->fast_state.sid())].mtx);
      ThreadRegistryLock lock1(&ctx->thread_registry);
      Lock lock2(&ctx->slot_mtx);
      Tid tid = kInvalidTid;
      VarSizeStackTrace stk;
      MutexSet mset;
      uptr tag = kExternalTagNone;
      bool res =
          RestoreStack(EventType::kAccessExt, thr->fast_state.sid(),
                       thr->fast_state.epoch(), 0x3000 + params.offset,
                       params.size, kAccessRead, &tid, &stk, &mset, &tag);
      CHECK_EQ(res, params.res);
      if (params.res) {
        CHECK_EQ(stk.size, 2);
        CHECK_EQ(stk.trace[0], 0x1000);
        CHECK_EQ(stk.trace[1], type ? 0x2000000 : 0x2000);
      }
    }
  }
}

TRACE_TEST(Trace, RestoreMutexLock) {
  // Check of restoration of a mutex lock event.
  ThreadArray<1> thr;
  TraceFunc(thr, 0x1000);
  TraceMutexLock(thr, EventType::kLock, 0x4000, 0x5000, 0x6000);
  TraceMutexLock(thr, EventType::kRLock, 0x4001, 0x5001, 0x6001);
  TraceMutexLock(thr, EventType::kRLock, 0x4002, 0x5001, 0x6002);
  Lock slot_lock(&ctx->slots[static_cast<uptr>(thr->fast_state.sid())].mtx);
  ThreadRegistryLock lock1(&ctx->thread_registry);
  Lock lock2(&ctx->slot_mtx);
  Tid tid = kInvalidTid;
  VarSizeStackTrace stk;
  MutexSet mset;
  uptr tag = kExternalTagNone;
  bool res = RestoreStack(EventType::kLock, thr->fast_state.sid(),
                          thr->fast_state.epoch(), 0x5001, 0, 0, &tid, &stk,
                          &mset, &tag);
  CHECK(res);
  CHECK_EQ(stk.size, 2);
  CHECK_EQ(stk.trace[0], 0x1000);
  CHECK_EQ(stk.trace[1], 0x4002);
  CHECK_EQ(mset.Size(), 2);
  CHECK_EQ(mset.Get(0).addr, 0x5000);
  CHECK_EQ(mset.Get(0).stack_id, 0x6000);
  CHECK_EQ(mset.Get(0).write, true);
  CHECK_EQ(mset.Get(1).addr, 0x5001);
  CHECK_EQ(mset.Get(1).stack_id, 0x6001);
  CHECK_EQ(mset.Get(1).write, false);
}

TRACE_TEST(Trace, MultiPart) {
  // Check replay of a trace with multiple parts.
  ThreadArray<1> thr;
  FuncEntry(thr, 0x1000);
  FuncEntry(thr, 0x2000);
  MutexPreLock(thr, 0x4000, 0x5000, 0);
  MutexPostLock(thr, 0x4000, 0x5000, 0);
  MutexPreLock(thr, 0x4000, 0x5000, 0);
  MutexPostLock(thr, 0x4000, 0x5000, 0);
  const uptr kEvents = 3 * sizeof(TracePart) / sizeof(Event);
  for (uptr i = 0; i < kEvents; i++) {
    FuncEntry(thr, 0x3000);
    MutexPreLock(thr, 0x4002, 0x5002, 0);
    MutexPostLock(thr, 0x4002, 0x5002, 0);
    MutexUnlock(thr, 0x4003, 0x5002, 0);
    FuncExit(thr);
  }
  FuncEntry(thr, 0x4000);
  TraceMutexLock(thr, EventType::kRLock, 0x4001, 0x5001, 0x6001);
  CHECK(TryTraceMemoryAccess(thr, 0x2002, 0x3000, 8, kAccessRead));
  Lock slot_lock(&ctx->slots[static_cast<uptr>(thr->fast_state.sid())].mtx);
  ThreadRegistryLock lock1(&ctx->thread_registry);
  Lock lock2(&ctx->slot_mtx);
  Tid tid = kInvalidTid;
  VarSizeStackTrace stk;
  MutexSet mset;
  uptr tag = kExternalTagNone;
  bool res = RestoreStack(EventType::kAccessExt, thr->fast_state.sid(),
                          thr->fast_state.epoch(), 0x3000, 8, kAccessRead, &tid,
                          &stk, &mset, &tag);
  CHECK(res);
  CHECK_EQ(tid, thr->tid);
  CHECK_EQ(stk.size, 4);
  CHECK_EQ(stk.trace[0], 0x1000);
  CHECK_EQ(stk.trace[1], 0x2000);
  CHECK_EQ(stk.trace[2], 0x4000);
  CHECK_EQ(stk.trace[3], 0x2002);
  CHECK_EQ(mset.Size(), 2);
  CHECK_EQ(mset.Get(0).addr, 0x5000);
  CHECK_EQ(mset.Get(0).write, true);
  CHECK_EQ(mset.Get(0).count, 2);
  CHECK_EQ(mset.Get(1).addr, 0x5001);
  CHECK_EQ(mset.Get(1).write, false);
  CHECK_EQ(mset.Get(1).count, 1);
}

TRACE_TEST(Trace, DeepSwitch) {
  ThreadArray<1> thr;
  for (int i = 0; i < 2000; i++) {
    FuncEntry(thr, 0x1000);
    const uptr kEvents = sizeof(TracePart) / sizeof(Event);
    for (uptr i = 0; i < kEvents; i++) {
      TraceMutexLock(thr, EventType::kLock, 0x4000, 0x5000, 0x6000);
      TraceMutexUnlock(thr, 0x5000);
    }
  }
}

void CheckTraceState(uptr count, uptr finished, uptr excess, uptr recycle) {
  Lock l(&ctx->slot_mtx);
  Printf("CheckTraceState(%zu/%zu, %zu/%zu, %zu/%zu, %zu/%zu)\n",
         ctx->trace_part_total_allocated, count,
         ctx->trace_part_recycle_finished, finished,
         ctx->trace_part_finished_excess, excess,
         ctx->trace_part_recycle.Size(), recycle);
  CHECK_EQ(ctx->trace_part_total_allocated, count);
  CHECK_EQ(ctx->trace_part_recycle_finished, finished);
  CHECK_EQ(ctx->trace_part_finished_excess, excess);
  CHECK_EQ(ctx->trace_part_recycle.Size(), recycle);
}

TRACE_TEST(TraceAlloc, SingleThread) {
  TraceResetForTesting();
  auto check_thread = [&](ThreadState *thr, uptr size, uptr count,
                          uptr finished, uptr excess, uptr recycle) {
    CHECK_EQ(thr->tctx->trace.parts.Size(), size);
    CheckTraceState(count, finished, excess, recycle);
  };
  ThreadArray<2> threads;
  check_thread(threads[0], 0, 0, 0, 0, 0);
  TraceSwitchPartImpl(threads[0]);
  check_thread(threads[0], 1, 1, 0, 0, 0);
  TraceSwitchPartImpl(threads[0]);
  check_thread(threads[0], 2, 2, 0, 0, 0);
  TraceSwitchPartImpl(threads[0]);
  check_thread(threads[0], 3, 3, 0, 0, 1);
  TraceSwitchPartImpl(threads[0]);
  check_thread(threads[0], 3, 3, 0, 0, 1);
  threads.Finish(0);
  CheckTraceState(3, 3, 0, 3);
  threads.Finish(1);
  CheckTraceState(3, 3, 0, 3);
}

TRACE_TEST(TraceAlloc, FinishedThreadReuse) {
  TraceResetForTesting();
  constexpr uptr Hi = Trace::kFinishedThreadHi;
  constexpr uptr kThreads = 4 * Hi;
  ThreadArray<kThreads> threads;
  for (uptr i = 0; i < kThreads; i++) {
    Printf("thread %zu\n", i);
    TraceSwitchPartImpl(threads[i]);
    if (i <= Hi)
      CheckTraceState(i + 1, i, 0, i);
    else if (i <= 2 * Hi)
      CheckTraceState(Hi + 1, Hi, i - Hi, Hi);
    else
      CheckTraceState(Hi + 1, Hi, Hi, Hi);
    threads.Finish(i);
    if (i < Hi)
      CheckTraceState(i + 1, i + 1, 0, i + 1);
    else if (i < 2 * Hi)
      CheckTraceState(Hi + 1, Hi + 1, i - Hi + 1, Hi + 1);
    else
      CheckTraceState(Hi + 1, Hi + 1, Hi + 1, Hi + 1);
  }
}

TRACE_TEST(TraceAlloc, FinishedThreadReuse2) {
  TraceResetForTesting();
  // constexpr uptr Lo = Trace::kFinishedThreadLo;
  // constexpr uptr Hi = Trace::kFinishedThreadHi;
  constexpr uptr Min = Trace::kMinParts;
  constexpr uptr kThreads = 10;
  constexpr uptr kParts = 2 * Min;
  ThreadArray<kThreads> threads;
  for (uptr i = 0; i < kThreads; i++) {
    Printf("thread %zu\n", i);
    for (uptr j = 0; j < kParts; j++) TraceSwitchPartImpl(threads[i]);
    if (i == 0)
      CheckTraceState(Min, 0, 0, 1);
    else
      CheckTraceState(2 * Min, 0, Min, Min + 1);
    threads.Finish(i);
    if (i == 0)
      CheckTraceState(Min, Min, 0, Min);
    else
      CheckTraceState(2 * Min, 2 * Min, Min, 2 * Min);
  }
}

}  // namespace __tsan
