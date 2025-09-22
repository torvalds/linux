//===-- tsan_go.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ThreadSanitizer runtime for Go language.
//
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"
#include "tsan_symbolize.h"
#include "sanitizer_common/sanitizer_common.h"
#include <stdlib.h>

namespace __tsan {

void InitializeInterceptors() {
}

void InitializeDynamicAnnotations() {
}

bool IsExpectedReport(uptr addr, uptr size) {
  return false;
}

void *Alloc(uptr sz) { return InternalAlloc(sz); }

void FreeImpl(void *p) { InternalFree(p); }

// Callback into Go.
static void (*go_runtime_cb)(uptr cmd, void *ctx);

enum {
  CallbackGetProc = 0,
  CallbackSymbolizeCode = 1,
  CallbackSymbolizeData = 2,
};

struct SymbolizeCodeContext {
  uptr pc;
  char *func;
  char *file;
  uptr line;
  uptr off;
  uptr res;
};

SymbolizedStack *SymbolizeCode(uptr addr) {
  SymbolizedStack *first = SymbolizedStack::New(addr);
  SymbolizedStack *s = first;
  for (;;) {
    SymbolizeCodeContext cbctx;
    internal_memset(&cbctx, 0, sizeof(cbctx));
    cbctx.pc = addr;
    go_runtime_cb(CallbackSymbolizeCode, &cbctx);
    if (cbctx.res == 0)
      break;
    AddressInfo &info = s->info;
    info.module_offset = cbctx.off;
    info.function = internal_strdup(cbctx.func ? cbctx.func : "??");
    info.file = internal_strdup(cbctx.file ? cbctx.file : "-");
    info.line = cbctx.line;
    info.column = 0;

    if (cbctx.pc == addr) // outermost (non-inlined) function
      break;
    addr = cbctx.pc;
    // Allocate a stack entry for the parent of the inlined function.
    SymbolizedStack *s2 = SymbolizedStack::New(addr);
    s->next = s2;
    s = s2;
  }
  return first;
}

struct SymbolizeDataContext {
  uptr addr;
  uptr heap;
  uptr start;
  uptr size;
  char *name;
  char *file;
  uptr line;
  uptr res;
};

ReportLocation *SymbolizeData(uptr addr) {
  SymbolizeDataContext cbctx;
  internal_memset(&cbctx, 0, sizeof(cbctx));
  cbctx.addr = addr;
  go_runtime_cb(CallbackSymbolizeData, &cbctx);
  if (!cbctx.res)
    return 0;
  if (cbctx.heap) {
    MBlock *b = ctx->metamap.GetBlock(cbctx.start);
    if (!b)
      return 0;
    auto *loc = New<ReportLocation>();
    loc->type = ReportLocationHeap;
    loc->heap_chunk_start = cbctx.start;
    loc->heap_chunk_size = b->siz;
    loc->tid = b->tid;
    loc->stack = SymbolizeStackId(b->stk);
    return loc;
  } else {
    auto *loc = New<ReportLocation>();
    loc->type = ReportLocationGlobal;
    loc->global.name = internal_strdup(cbctx.name ? cbctx.name : "??");
    loc->global.file = internal_strdup(cbctx.file ? cbctx.file : "??");
    loc->global.line = cbctx.line;
    loc->global.start = cbctx.start;
    loc->global.size = cbctx.size;
    return loc;
  }
}

static ThreadState *main_thr;
static bool inited;

static Processor* get_cur_proc() {
  if (UNLIKELY(!inited)) {
    // Running Initialize().
    // We have not yet returned the Processor to Go, so we cannot ask it back.
    // Currently, Initialize() does not use the Processor, so return nullptr.
    return nullptr;
  }
  Processor *proc;
  go_runtime_cb(CallbackGetProc, &proc);
  return proc;
}

Processor *ThreadState::proc() {
  return get_cur_proc();
}

extern "C" {

static ThreadState *AllocGoroutine() {
  auto *thr = (ThreadState *)Alloc(sizeof(ThreadState));
  internal_memset(thr, 0, sizeof(*thr));
  return thr;
}

void __tsan_init(ThreadState **thrp, Processor **procp,
                 void (*cb)(uptr cmd, void *cb)) {
  go_runtime_cb = cb;
  ThreadState *thr = AllocGoroutine();
  main_thr = *thrp = thr;
  Initialize(thr);
  *procp = thr->proc1;
  inited = true;
}

void __tsan_fini() {
  // FIXME: Not necessary thread 0.
  ThreadState *thr = main_thr;
  int res = Finalize(thr);
  exit(res);
}

void __tsan_map_shadow(uptr addr, uptr size) {
  MapShadow(addr, size);
}

void __tsan_read(ThreadState *thr, void *addr, void *pc) {
  MemoryAccess(thr, (uptr)pc, (uptr)addr, 1, kAccessRead);
}

void __tsan_read_pc(ThreadState *thr, void *addr, uptr callpc, uptr pc) {
  if (callpc != 0)
    FuncEntry(thr, callpc);
  MemoryAccess(thr, (uptr)pc, (uptr)addr, 1, kAccessRead);
  if (callpc != 0)
    FuncExit(thr);
}

void __tsan_write(ThreadState *thr, void *addr, void *pc) {
  MemoryAccess(thr, (uptr)pc, (uptr)addr, 1, kAccessWrite);
}

void __tsan_write_pc(ThreadState *thr, void *addr, uptr callpc, uptr pc) {
  if (callpc != 0)
    FuncEntry(thr, callpc);
  MemoryAccess(thr, (uptr)pc, (uptr)addr, 1, kAccessWrite);
  if (callpc != 0)
    FuncExit(thr);
}

void __tsan_read_range(ThreadState *thr, void *addr, uptr size, uptr pc) {
  MemoryAccessRange(thr, (uptr)pc, (uptr)addr, size, false);
}

void __tsan_write_range(ThreadState *thr, void *addr, uptr size, uptr pc) {
  MemoryAccessRange(thr, (uptr)pc, (uptr)addr, size, true);
}

void __tsan_func_enter(ThreadState *thr, void *pc) {
  FuncEntry(thr, (uptr)pc);
}

void __tsan_func_exit(ThreadState *thr) {
  FuncExit(thr);
}

void __tsan_malloc(ThreadState *thr, uptr pc, uptr p, uptr sz) {
  CHECK(inited);
  if (thr && pc)
    ctx->metamap.AllocBlock(thr, pc, p, sz);
  MemoryResetRange(thr, pc, (uptr)p, sz);
}

void __tsan_free(uptr p, uptr sz) {
  ctx->metamap.FreeRange(get_cur_proc(), p, sz, false);
}

void __tsan_go_start(ThreadState *parent, ThreadState **pthr, void *pc) {
  ThreadState *thr = AllocGoroutine();
  *pthr = thr;
  Tid goid = ThreadCreate(parent, (uptr)pc, 0, true);
  ThreadStart(thr, goid, 0, ThreadType::Regular);
}

void __tsan_go_end(ThreadState *thr) {
  ThreadFinish(thr);
  Free(thr);
}

void __tsan_proc_create(Processor **pproc) {
  *pproc = ProcCreate();
}

void __tsan_proc_destroy(Processor *proc) {
  ProcDestroy(proc);
}

void __tsan_acquire(ThreadState *thr, void *addr) {
  Acquire(thr, 0, (uptr)addr);
}

void __tsan_release_acquire(ThreadState *thr, void *addr) {
  ReleaseStoreAcquire(thr, 0, (uptr)addr);
}

void __tsan_release(ThreadState *thr, void *addr) {
  ReleaseStore(thr, 0, (uptr)addr);
}

void __tsan_release_merge(ThreadState *thr, void *addr) {
  Release(thr, 0, (uptr)addr);
}

void __tsan_finalizer_goroutine(ThreadState *thr) { AcquireGlobal(thr); }

void __tsan_mutex_before_lock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexPreLock(thr, 0, addr);
  else
    MutexPreReadLock(thr, 0, addr);
}

void __tsan_mutex_after_lock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexPostLock(thr, 0, addr);
  else
    MutexPostReadLock(thr, 0, addr);
}

void __tsan_mutex_before_unlock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexUnlock(thr, 0, addr);
  else
    MutexReadUnlock(thr, 0, addr);
}

void __tsan_go_ignore_sync_begin(ThreadState *thr) {
  ThreadIgnoreSyncBegin(thr, 0);
}

void __tsan_go_ignore_sync_end(ThreadState *thr) { ThreadIgnoreSyncEnd(thr); }

void __tsan_report_count(u64 *pn) {
  Lock lock(&ctx->report_mtx);
  *pn = ctx->nreported;
}

}  // extern "C"
}  // namespace __tsan
