//===-- tsan_debugging.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// TSan debugging API implementation.
//===----------------------------------------------------------------------===//
#include "tsan_interface.h"
#include "tsan_report.h"
#include "tsan_rtl.h"

#include "sanitizer_common/sanitizer_stackdepot.h"

using namespace __tsan;

static const char *ReportTypeDescription(ReportType typ) {
  switch (typ) {
    case ReportTypeRace: return "data-race";
    case ReportTypeVptrRace: return "data-race-vptr";
    case ReportTypeUseAfterFree: return "heap-use-after-free";
    case ReportTypeVptrUseAfterFree: return "heap-use-after-free-vptr";
    case ReportTypeExternalRace: return "external-race";
    case ReportTypeThreadLeak: return "thread-leak";
    case ReportTypeMutexDestroyLocked: return "locked-mutex-destroy";
    case ReportTypeMutexDoubleLock: return "mutex-double-lock";
    case ReportTypeMutexInvalidAccess: return "mutex-invalid-access";
    case ReportTypeMutexBadUnlock: return "mutex-bad-unlock";
    case ReportTypeMutexBadReadLock: return "mutex-bad-read-lock";
    case ReportTypeMutexBadReadUnlock: return "mutex-bad-read-unlock";
    case ReportTypeSignalUnsafe: return "signal-unsafe-call";
    case ReportTypeErrnoInSignal: return "errno-in-signal-handler";
    case ReportTypeDeadlock: return "lock-order-inversion";
    case ReportTypeMutexHeldWrongContext:
      return "mutex-held-in-wrong-context";
      // No default case so compiler warns us if we miss one
  }
  UNREACHABLE("missing case");
}

static const char *ReportLocationTypeDescription(ReportLocationType typ) {
  switch (typ) {
    case ReportLocationGlobal: return "global";
    case ReportLocationHeap: return "heap";
    case ReportLocationStack: return "stack";
    case ReportLocationTLS: return "tls";
    case ReportLocationFD: return "fd";
    // No default case so compiler warns us if we miss one
  }
  UNREACHABLE("missing case");
}

static void CopyTrace(SymbolizedStack *first_frame, void **trace,
                      uptr trace_size) {
  uptr i = 0;
  for (SymbolizedStack *frame = first_frame; frame != nullptr;
       frame = frame->next) {
    trace[i++] = (void *)frame->info.address;
    if (i >= trace_size) break;
  }
}

// Meant to be called by the debugger.
SANITIZER_INTERFACE_ATTRIBUTE
void *__tsan_get_current_report() {
  return const_cast<ReportDesc*>(cur_thread()->current_report);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_data(void *report, const char **description, int *count,
                           int *stack_count, int *mop_count, int *loc_count,
                           int *mutex_count, int *thread_count,
                           int *unique_tid_count, void **sleep_trace,
                           uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  *description = ReportTypeDescription(rep->typ);
  *count = rep->count;
  *stack_count = rep->stacks.Size();
  *mop_count = rep->mops.Size();
  *loc_count = rep->locs.Size();
  *mutex_count = rep->mutexes.Size();
  *thread_count = rep->threads.Size();
  *unique_tid_count = rep->unique_tids.Size();
  if (rep->sleep) CopyTrace(rep->sleep->frames, sleep_trace, trace_size);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_tag(void *report, uptr *tag) {
  const ReportDesc *rep = (ReportDesc *)report;
  *tag = rep->tag;
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_stack(void *report, uptr idx, void **trace,
                            uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->stacks.Size());
  ReportStack *stack = rep->stacks[idx];
  if (stack) CopyTrace(stack->frames, trace, trace_size);
  return stack ? 1 : 0;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_mop(void *report, uptr idx, int *tid, void **addr,
                          int *size, int *write, int *atomic, void **trace,
                          uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->mops.Size());
  ReportMop *mop = rep->mops[idx];
  *tid = mop->tid;
  *addr = (void *)mop->addr;
  *size = mop->size;
  *write = mop->write ? 1 : 0;
  *atomic = mop->atomic ? 1 : 0;
  if (mop->stack) CopyTrace(mop->stack->frames, trace, trace_size);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_loc(void *report, uptr idx, const char **type,
                          void **addr, uptr *start, uptr *size, int *tid,
                          int *fd, int *suppressable, void **trace,
                          uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->locs.Size());
  ReportLocation *loc = rep->locs[idx];
  *type = ReportLocationTypeDescription(loc->type);
  *addr = (void *)loc->global.start;
  *start = loc->heap_chunk_start;
  *size = loc->heap_chunk_size;
  *tid = loc->tid;
  *fd = loc->fd;
  *suppressable = loc->suppressable;
  if (loc->stack) CopyTrace(loc->stack->frames, trace, trace_size);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_loc_object_type(void *report, uptr idx,
                                      const char **object_type) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->locs.Size());
  ReportLocation *loc = rep->locs[idx];
  *object_type = GetObjectTypeFromTag(loc->external_tag);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_mutex(void *report, uptr idx, uptr *mutex_id, void **addr,
                            int *destroyed, void **trace, uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->mutexes.Size());
  ReportMutex *mutex = rep->mutexes[idx];
  *mutex_id = mutex->id;
  *addr = (void *)mutex->addr;
  *destroyed = false;
  if (mutex->stack) CopyTrace(mutex->stack->frames, trace, trace_size);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_thread(void *report, uptr idx, int *tid, tid_t *os_id,
                             int *running, const char **name, int *parent_tid,
                             void **trace, uptr trace_size) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->threads.Size());
  ReportThread *thread = rep->threads[idx];
  *tid = thread->id;
  *os_id = thread->os_id;
  *running = thread->running;
  *name = thread->name;
  *parent_tid = thread->parent_tid;
  if (thread->stack) CopyTrace(thread->stack->frames, trace, trace_size);
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_report_unique_tid(void *report, uptr idx, int *tid) {
  const ReportDesc *rep = (ReportDesc *)report;
  CHECK_LT(idx, rep->unique_tids.Size());
  *tid = rep->unique_tids[idx];
  return 1;
}

SANITIZER_INTERFACE_ATTRIBUTE
const char *__tsan_locate_address(uptr addr, char *name, uptr name_size,
                                  uptr *region_address_ptr,
                                  uptr *region_size_ptr) {
  uptr region_address = 0;
  uptr region_size = 0;
  const char *region_kind = nullptr;
  if (name && name_size > 0) name[0] = 0;

  if (IsMetaMem(reinterpret_cast<u32 *>(addr))) {
    region_kind = "meta shadow";
  } else if (IsShadowMem(reinterpret_cast<RawShadow *>(addr))) {
    region_kind = "shadow";
  } else {
    bool is_stack = false;
    MBlock *b = 0;
    Allocator *a = allocator();
    if (a->PointerIsMine((void *)addr)) {
      void *block_begin = a->GetBlockBegin((void *)addr);
      if (block_begin) b = ctx->metamap.GetBlock((uptr)block_begin);
    }

    if (b != 0) {
      region_address = (uptr)allocator()->GetBlockBegin((void *)addr);
      region_size = b->siz;
      region_kind = "heap";
    } else {
      // TODO(kuba.brecka): We should not lock. This is supposed to be called
      // from within the debugger when other threads are stopped.
      ctx->thread_registry.Lock();
      ThreadContext *tctx = IsThreadStackOrTls(addr, &is_stack);
      ctx->thread_registry.Unlock();
      if (tctx) {
        region_kind = is_stack ? "stack" : "tls";
      } else {
        region_kind = "global";
        DataInfo info;
        if (Symbolizer::GetOrInit()->SymbolizeData(addr, &info)) {
          internal_strncpy(name, info.name, name_size);
          region_address = info.start;
          region_size = info.size;
        }
      }
    }
  }

  CHECK(region_kind);
  if (region_address_ptr) *region_address_ptr = region_address;
  if (region_size_ptr) *region_size_ptr = region_size;
  return region_kind;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __tsan_get_alloc_stack(uptr addr, uptr *trace, uptr size, int *thread_id,
                           tid_t *os_id) {
  MBlock *b = 0;
  Allocator *a = allocator();
  if (a->PointerIsMine((void *)addr)) {
    void *block_begin = a->GetBlockBegin((void *)addr);
    if (block_begin) b = ctx->metamap.GetBlock((uptr)block_begin);
  }
  if (b == 0) return 0;

  *thread_id = b->tid;
  // No locking.  This is supposed to be called from within the debugger when
  // other threads are stopped.
  ThreadContextBase *tctx = ctx->thread_registry.GetThreadLocked(b->tid);
  *os_id = tctx->os_id;

  StackTrace stack = StackDepotGet(b->stk);
  size = Min(size, (uptr)stack.size);
  for (uptr i = 0; i < size; i++) trace[i] = stack.trace[stack.size - i - 1];
  return size;
}
