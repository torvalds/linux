//===-- tsan_interface_ann.cpp --------------------------------------------===//
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
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_vector.h"
#include "tsan_interface_ann.h"
#include "tsan_report.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_flags.h"
#include "tsan_platform.h"

#define CALLERPC ((uptr)__builtin_return_address(0))

using namespace __tsan;

namespace __tsan {

class ScopedAnnotation {
 public:
  ScopedAnnotation(ThreadState *thr, const char *aname, uptr pc)
      : thr_(thr) {
    FuncEntry(thr_, pc);
    DPrintf("#%d: annotation %s()\n", thr_->tid, aname);
  }

  ~ScopedAnnotation() {
    FuncExit(thr_);
    CheckedMutex::CheckNoLocks();
  }
 private:
  ThreadState *const thr_;
};

#define SCOPED_ANNOTATION_RET(typ, ret)                     \
  if (!flags()->enable_annotations)                         \
    return ret;                                             \
  ThreadState *thr = cur_thread();                          \
  const uptr caller_pc = (uptr)__builtin_return_address(0); \
  ScopedAnnotation sa(thr, __func__, caller_pc);            \
  const uptr pc = StackTrace::GetCurrentPc();               \
  (void)pc;

#define SCOPED_ANNOTATION(typ) SCOPED_ANNOTATION_RET(typ, )

static const int kMaxDescLen = 128;

struct ExpectRace {
  ExpectRace *next;
  ExpectRace *prev;
  atomic_uintptr_t hitcount;
  atomic_uintptr_t addcount;
  uptr addr;
  uptr size;
  char *file;
  int line;
  char desc[kMaxDescLen];
};

struct DynamicAnnContext {
  Mutex mtx;
  ExpectRace benign;

  DynamicAnnContext() : mtx(MutexTypeAnnotations) {}
};

static DynamicAnnContext *dyn_ann_ctx;
alignas(64) static char dyn_ann_ctx_placeholder[sizeof(DynamicAnnContext)];

static void AddExpectRace(ExpectRace *list,
    char *f, int l, uptr addr, uptr size, char *desc) {
  ExpectRace *race = list->next;
  for (; race != list; race = race->next) {
    if (race->addr == addr && race->size == size) {
      atomic_store_relaxed(&race->addcount,
          atomic_load_relaxed(&race->addcount) + 1);
      return;
    }
  }
  race = static_cast<ExpectRace *>(Alloc(sizeof(ExpectRace)));
  race->addr = addr;
  race->size = size;
  race->file = f;
  race->line = l;
  race->desc[0] = 0;
  atomic_store_relaxed(&race->hitcount, 0);
  atomic_store_relaxed(&race->addcount, 1);
  if (desc) {
    int i = 0;
    for (; i < kMaxDescLen - 1 && desc[i]; i++)
      race->desc[i] = desc[i];
    race->desc[i] = 0;
  }
  race->prev = list;
  race->next = list->next;
  race->next->prev = race;
  list->next = race;
}

static ExpectRace *FindRace(ExpectRace *list, uptr addr, uptr size) {
  for (ExpectRace *race = list->next; race != list; race = race->next) {
    uptr maxbegin = max(race->addr, addr);
    uptr minend = min(race->addr + race->size, addr + size);
    if (maxbegin < minend)
      return race;
  }
  return 0;
}

static bool CheckContains(ExpectRace *list, uptr addr, uptr size) {
  ExpectRace *race = FindRace(list, addr, size);
  if (race == 0)
    return false;
  DPrintf("Hit expected/benign race: %s addr=%zx:%d %s:%d\n",
      race->desc, race->addr, (int)race->size, race->file, race->line);
  atomic_fetch_add(&race->hitcount, 1, memory_order_relaxed);
  return true;
}

static void InitList(ExpectRace *list) {
  list->next = list;
  list->prev = list;
}

void InitializeDynamicAnnotations() {
  dyn_ann_ctx = new(dyn_ann_ctx_placeholder) DynamicAnnContext;
  InitList(&dyn_ann_ctx->benign);
}

bool IsExpectedReport(uptr addr, uptr size) {
  ReadLock lock(&dyn_ann_ctx->mtx);
  return CheckContains(&dyn_ann_ctx->benign, addr, size);
}
}  // namespace __tsan

using namespace __tsan;

extern "C" {
void INTERFACE_ATTRIBUTE AnnotateHappensBefore(char *f, int l, uptr addr) {
  SCOPED_ANNOTATION(AnnotateHappensBefore);
  Release(thr, pc, addr);
}

void INTERFACE_ATTRIBUTE AnnotateHappensAfter(char *f, int l, uptr addr) {
  SCOPED_ANNOTATION(AnnotateHappensAfter);
  Acquire(thr, pc, addr);
}

void INTERFACE_ATTRIBUTE AnnotateCondVarSignal(char *f, int l, uptr cv) {
}

void INTERFACE_ATTRIBUTE AnnotateCondVarSignalAll(char *f, int l, uptr cv) {
}

void INTERFACE_ATTRIBUTE AnnotateMutexIsNotPHB(char *f, int l, uptr mu) {
}

void INTERFACE_ATTRIBUTE AnnotateCondVarWait(char *f, int l, uptr cv,
                                             uptr lock) {
}

void INTERFACE_ATTRIBUTE AnnotateRWLockCreate(char *f, int l, uptr m) {
  SCOPED_ANNOTATION(AnnotateRWLockCreate);
  MutexCreate(thr, pc, m, MutexFlagWriteReentrant);
}

void INTERFACE_ATTRIBUTE AnnotateRWLockCreateStatic(char *f, int l, uptr m) {
  SCOPED_ANNOTATION(AnnotateRWLockCreateStatic);
  MutexCreate(thr, pc, m, MutexFlagWriteReentrant | MutexFlagLinkerInit);
}

void INTERFACE_ATTRIBUTE AnnotateRWLockDestroy(char *f, int l, uptr m) {
  SCOPED_ANNOTATION(AnnotateRWLockDestroy);
  MutexDestroy(thr, pc, m);
}

void INTERFACE_ATTRIBUTE AnnotateRWLockAcquired(char *f, int l, uptr m,
                                                uptr is_w) {
  SCOPED_ANNOTATION(AnnotateRWLockAcquired);
  if (is_w)
    MutexPostLock(thr, pc, m, MutexFlagDoPreLockOnPostLock);
  else
    MutexPostReadLock(thr, pc, m, MutexFlagDoPreLockOnPostLock);
}

void INTERFACE_ATTRIBUTE AnnotateRWLockReleased(char *f, int l, uptr m,
                                                uptr is_w) {
  SCOPED_ANNOTATION(AnnotateRWLockReleased);
  if (is_w)
    MutexUnlock(thr, pc, m);
  else
    MutexReadUnlock(thr, pc, m);
}

void INTERFACE_ATTRIBUTE AnnotateTraceMemory(char *f, int l, uptr mem) {
}

void INTERFACE_ATTRIBUTE AnnotateFlushState(char *f, int l) {
}

void INTERFACE_ATTRIBUTE AnnotateNewMemory(char *f, int l, uptr mem,
                                           uptr size) {
}

void INTERFACE_ATTRIBUTE AnnotateNoOp(char *f, int l, uptr mem) {
}

void INTERFACE_ATTRIBUTE AnnotateFlushExpectedRaces(char *f, int l) {
}

void INTERFACE_ATTRIBUTE AnnotateEnableRaceDetection(
    char *f, int l, int enable) {
}

void INTERFACE_ATTRIBUTE AnnotateMutexIsUsedAsCondVar(
    char *f, int l, uptr mu) {
}

void INTERFACE_ATTRIBUTE AnnotatePCQGet(
    char *f, int l, uptr pcq) {
}

void INTERFACE_ATTRIBUTE AnnotatePCQPut(
    char *f, int l, uptr pcq) {
}

void INTERFACE_ATTRIBUTE AnnotatePCQDestroy(
    char *f, int l, uptr pcq) {
}

void INTERFACE_ATTRIBUTE AnnotatePCQCreate(
    char *f, int l, uptr pcq) {
}

void INTERFACE_ATTRIBUTE AnnotateExpectRace(
    char *f, int l, uptr mem, char *desc) {
}

static void BenignRaceImpl(char *f, int l, uptr mem, uptr size, char *desc) {
  Lock lock(&dyn_ann_ctx->mtx);
  AddExpectRace(&dyn_ann_ctx->benign,
                f, l, mem, size, desc);
  DPrintf("Add benign race: %s addr=%zx %s:%d\n", desc, mem, f, l);
}

void INTERFACE_ATTRIBUTE AnnotateBenignRaceSized(
    char *f, int l, uptr mem, uptr size, char *desc) {
  SCOPED_ANNOTATION(AnnotateBenignRaceSized);
  BenignRaceImpl(f, l, mem, size, desc);
}

void INTERFACE_ATTRIBUTE AnnotateBenignRace(
    char *f, int l, uptr mem, char *desc) {
  SCOPED_ANNOTATION(AnnotateBenignRace);
  BenignRaceImpl(f, l, mem, 1, desc);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreReadsBegin(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreReadsBegin);
  ThreadIgnoreBegin(thr, pc);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreReadsEnd(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreReadsEnd);
  ThreadIgnoreEnd(thr);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreWritesBegin(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreWritesBegin);
  ThreadIgnoreBegin(thr, pc);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreWritesEnd(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreWritesEnd);
  ThreadIgnoreEnd(thr);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreSyncBegin(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreSyncBegin);
  ThreadIgnoreSyncBegin(thr, pc);
}

void INTERFACE_ATTRIBUTE AnnotateIgnoreSyncEnd(char *f, int l) {
  SCOPED_ANNOTATION(AnnotateIgnoreSyncEnd);
  ThreadIgnoreSyncEnd(thr);
}

void INTERFACE_ATTRIBUTE AnnotatePublishMemoryRange(
    char *f, int l, uptr addr, uptr size) {
}

void INTERFACE_ATTRIBUTE AnnotateUnpublishMemoryRange(
    char *f, int l, uptr addr, uptr size) {
}

void INTERFACE_ATTRIBUTE AnnotateThreadName(
    char *f, int l, char *name) {
  SCOPED_ANNOTATION(AnnotateThreadName);
  ThreadSetName(thr, name);
}

// We deliberately omit the implementation of WTFAnnotateHappensBefore() and
// WTFAnnotateHappensAfter(). Those are being used by Webkit to annotate
// atomic operations, which should be handled by ThreadSanitizer correctly.
void INTERFACE_ATTRIBUTE WTFAnnotateHappensBefore(char *f, int l, uptr addr) {
}

void INTERFACE_ATTRIBUTE WTFAnnotateHappensAfter(char *f, int l, uptr addr) {
}

void INTERFACE_ATTRIBUTE WTFAnnotateBenignRaceSized(
    char *f, int l, uptr mem, uptr sz, char *desc) {
  SCOPED_ANNOTATION(AnnotateBenignRaceSized);
  BenignRaceImpl(f, l, mem, sz, desc);
}

int INTERFACE_ATTRIBUTE RunningOnValgrind() {
  return flags()->running_on_valgrind;
}

double __attribute__((weak)) INTERFACE_ATTRIBUTE ValgrindSlowdown(void) {
  return 10.0;
}

const char INTERFACE_ATTRIBUTE* ThreadSanitizerQuery(const char *query) {
  if (internal_strcmp(query, "pure_happens_before") == 0)
    return "1";
  else
    return "0";
}

void INTERFACE_ATTRIBUTE
AnnotateMemoryIsInitialized(char *f, int l, uptr mem, uptr sz) {}
void INTERFACE_ATTRIBUTE
AnnotateMemoryIsUninitialized(char *f, int l, uptr mem, uptr sz) {}

// Note: the parameter is called flagz, because flags is already taken
// by the global function that returns flags.
INTERFACE_ATTRIBUTE
void __tsan_mutex_create(void *m, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_create);
  MutexCreate(thr, pc, (uptr)m, flagz & MutexCreationFlagMask);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_destroy(void *m, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_destroy);
  MutexDestroy(thr, pc, (uptr)m, flagz);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_pre_lock(void *m, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_pre_lock);
  if (!(flagz & MutexFlagTryLock)) {
    if (flagz & MutexFlagReadLock)
      MutexPreReadLock(thr, pc, (uptr)m);
    else
      MutexPreLock(thr, pc, (uptr)m);
  }
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_post_lock(void *m, unsigned flagz, int rec) {
  SCOPED_ANNOTATION(__tsan_mutex_post_lock);
  ThreadIgnoreSyncEnd(thr);
  ThreadIgnoreEnd(thr);
  if (!(flagz & MutexFlagTryLockFailed)) {
    if (flagz & MutexFlagReadLock)
      MutexPostReadLock(thr, pc, (uptr)m, flagz);
    else
      MutexPostLock(thr, pc, (uptr)m, flagz, rec);
  }
}

INTERFACE_ATTRIBUTE
int __tsan_mutex_pre_unlock(void *m, unsigned flagz) {
  SCOPED_ANNOTATION_RET(__tsan_mutex_pre_unlock, 0);
  int ret = 0;
  if (flagz & MutexFlagReadLock) {
    CHECK(!(flagz & MutexFlagRecursiveUnlock));
    MutexReadUnlock(thr, pc, (uptr)m);
  } else {
    ret = MutexUnlock(thr, pc, (uptr)m, flagz);
  }
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
  return ret;
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_post_unlock(void *m, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_post_unlock);
  ThreadIgnoreSyncEnd(thr);
  ThreadIgnoreEnd(thr);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_pre_signal(void *addr, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_pre_signal);
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_post_signal(void *addr, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_post_signal);
  ThreadIgnoreSyncEnd(thr);
  ThreadIgnoreEnd(thr);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_pre_divert(void *addr, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_pre_divert);
  // Exit from ignore region started in __tsan_mutex_pre_lock/unlock/signal.
  ThreadIgnoreSyncEnd(thr);
  ThreadIgnoreEnd(thr);
}

INTERFACE_ATTRIBUTE
void __tsan_mutex_post_divert(void *addr, unsigned flagz) {
  SCOPED_ANNOTATION(__tsan_mutex_post_divert);
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
}

static void ReportMutexHeldWrongContext(ThreadState *thr, uptr pc) {
  ThreadRegistryLock l(&ctx->thread_registry);
  ScopedReport rep(ReportTypeMutexHeldWrongContext);
  for (uptr i = 0; i < thr->mset.Size(); ++i) {
    MutexSet::Desc desc = thr->mset.Get(i);
    rep.AddMutex(desc.addr, desc.stack_id);
  }
  VarSizeStackTrace trace;
  ObtainCurrentStack(thr, pc, &trace);
  rep.AddStack(trace, true);
  OutputReport(thr, rep);
}

INTERFACE_ATTRIBUTE
void __tsan_check_no_mutexes_held() {
  SCOPED_ANNOTATION(__tsan_check_no_mutexes_held);
  if (thr->mset.Size() == 0) {
    return;
  }
  ReportMutexHeldWrongContext(thr, pc);
}
}  // extern "C"
