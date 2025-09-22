//===-- dd_rtl.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dd_rtl.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __dsan {

static Context *ctx;

static u32 CurrentStackTrace(Thread *thr, uptr skip) {
  BufferedStackTrace stack;
  thr->ignore_interceptors = true;
  stack.Unwind(1000, 0, 0, 0, 0, 0, false);
  thr->ignore_interceptors = false;
  if (stack.size <= skip)
    return 0;
  return StackDepotPut(StackTrace(stack.trace + skip, stack.size - skip));
}

static void PrintStackTrace(Thread *thr, u32 stk) {
  StackTrace stack = StackDepotGet(stk);
  thr->ignore_interceptors = true;
  stack.Print();
  thr->ignore_interceptors = false;
}

static void ReportDeadlock(Thread *thr, DDReport *rep) {
  if (rep == 0)
    return;
  Lock lock(&ctx->report_mutex);
  Printf("==============================\n");
  Printf("WARNING: lock-order-inversion (potential deadlock)\n");
  for (int i = 0; i < rep->n; i++) {
    Printf("Thread %lld locks mutex %llu while holding mutex %llu:\n",
           rep->loop[i].thr_ctx, rep->loop[i].mtx_ctx1, rep->loop[i].mtx_ctx0);
    PrintStackTrace(thr, rep->loop[i].stk[1]);
    if (rep->loop[i].stk[0]) {
      Printf("Mutex %llu was acquired here:\n",
        rep->loop[i].mtx_ctx0);
      PrintStackTrace(thr, rep->loop[i].stk[0]);
    }
  }
  Printf("==============================\n");
}

Callback::Callback(Thread *thr)
    : thr(thr) {
  lt = thr->dd_lt;
  pt = thr->dd_pt;
}

u32 Callback::Unwind() {
  return CurrentStackTrace(thr, 3);
}

static void InitializeFlags() {
  Flags *f = flags();

  // Default values.
  f->second_deadlock_stack = false;

  SetCommonFlagsDefaults();
  {
    // Override some common flags defaults.
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.allow_addr2line = true;
    OverrideCommonFlags(cf);
  }

  // Override from command line.
  FlagParser parser;
  RegisterFlag(&parser, "second_deadlock_stack", "", &f->second_deadlock_stack);
  RegisterCommonFlags(&parser);
  parser.ParseStringFromEnv("DSAN_OPTIONS");
  SetVerbosity(common_flags()->verbosity);
}

void Initialize() {
  static u64 ctx_mem[sizeof(Context) / sizeof(u64) + 1];
  ctx = new(ctx_mem) Context();

  InitializeInterceptors();
  InitializeFlags();
  ctx->dd = DDetector::Create(flags());
}

void ThreadInit(Thread *thr) {
  static atomic_uintptr_t id_gen;
  uptr id = atomic_fetch_add(&id_gen, 1, memory_order_relaxed);
  thr->dd_pt = ctx->dd->CreatePhysicalThread();
  thr->dd_lt = ctx->dd->CreateLogicalThread(id);
}

void ThreadDestroy(Thread *thr) {
  ctx->dd->DestroyPhysicalThread(thr->dd_pt);
  ctx->dd->DestroyLogicalThread(thr->dd_lt);
}

void MutexBeforeLock(Thread *thr, uptr m, bool writelock) {
  if (thr->ignore_interceptors)
    return;
  Callback cb(thr);
  {
    MutexHashMap::Handle h(&ctx->mutex_map, m);
    if (h.created())
      ctx->dd->MutexInit(&cb, &h->dd);
    ctx->dd->MutexBeforeLock(&cb, &h->dd, writelock);
  }
  ReportDeadlock(thr, ctx->dd->GetReport(&cb));
}

void MutexAfterLock(Thread *thr, uptr m, bool writelock, bool trylock) {
  if (thr->ignore_interceptors)
    return;
  Callback cb(thr);
  {
    MutexHashMap::Handle h(&ctx->mutex_map, m);
    if (h.created())
      ctx->dd->MutexInit(&cb, &h->dd);
    ctx->dd->MutexAfterLock(&cb, &h->dd, writelock, trylock);
  }
  ReportDeadlock(thr, ctx->dd->GetReport(&cb));
}

void MutexBeforeUnlock(Thread *thr, uptr m, bool writelock) {
  if (thr->ignore_interceptors)
    return;
  Callback cb(thr);
  {
    MutexHashMap::Handle h(&ctx->mutex_map, m);
    ctx->dd->MutexBeforeUnlock(&cb, &h->dd, writelock);
  }
  ReportDeadlock(thr, ctx->dd->GetReport(&cb));
}

void MutexDestroy(Thread *thr, uptr m) {
  if (thr->ignore_interceptors)
    return;
  Callback cb(thr);
  MutexHashMap::Handle h(&ctx->mutex_map, m, true);
  if (!h.exists())
    return;
  ctx->dd->MutexDestroy(&cb, &h->dd);
}

}  // namespace __dsan
