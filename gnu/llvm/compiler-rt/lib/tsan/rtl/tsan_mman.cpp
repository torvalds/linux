//===-- tsan_mman.cpp -----------------------------------------------------===//
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
#include "tsan_mman.h"

#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "tsan_flags.h"
#include "tsan_interface.h"
#include "tsan_report.h"
#include "tsan_rtl.h"

namespace __tsan {

struct MapUnmapCallback {
  void OnMap(uptr p, uptr size) const { }
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {};
  void OnUnmap(uptr p, uptr size) const {
    // We are about to unmap a chunk of user memory.
    // Mark the corresponding shadow memory as not needed.
    DontNeedShadowFor(p, size);
    // Mark the corresponding meta shadow memory as not needed.
    // Note the block does not contain any meta info at this point
    // (this happens after free).
    const uptr kMetaRatio = kMetaShadowCell / kMetaShadowSize;
    const uptr kPageSize = GetPageSizeCached() * kMetaRatio;
    // Block came from LargeMmapAllocator, so must be large.
    // We rely on this in the calculations below.
    CHECK_GE(size, 2 * kPageSize);
    uptr diff = RoundUp(p, kPageSize) - p;
    if (diff != 0) {
      p += diff;
      size -= diff;
    }
    diff = p + size - RoundDown(p + size, kPageSize);
    if (diff != 0)
      size -= diff;
    uptr p_meta = (uptr)MemToMeta(p);
    ReleaseMemoryPagesToOS(p_meta, p_meta + size / kMetaRatio);
  }
};

alignas(64) static char allocator_placeholder[sizeof(Allocator)];
Allocator *allocator() {
  return reinterpret_cast<Allocator*>(&allocator_placeholder);
}

struct GlobalProc {
  Mutex mtx;
  Processor *proc;
  // This mutex represents the internal allocator combined for
  // the purposes of deadlock detection. The internal allocator
  // uses multiple mutexes, moreover they are locked only occasionally
  // and they are spin mutexes which don't support deadlock detection.
  // So we use this fake mutex to serve as a substitute for these mutexes.
  CheckedMutex internal_alloc_mtx;

  GlobalProc()
      : mtx(MutexTypeGlobalProc),
        proc(ProcCreate()),
        internal_alloc_mtx(MutexTypeInternalAlloc) {}
};

alignas(64) static char global_proc_placeholder[sizeof(GlobalProc)];
GlobalProc *global_proc() {
  return reinterpret_cast<GlobalProc*>(&global_proc_placeholder);
}

static void InternalAllocAccess() {
  global_proc()->internal_alloc_mtx.Lock();
  global_proc()->internal_alloc_mtx.Unlock();
}

ScopedGlobalProcessor::ScopedGlobalProcessor() {
  GlobalProc *gp = global_proc();
  ThreadState *thr = cur_thread();
  if (thr->proc())
    return;
  // If we don't have a proc, use the global one.
  // There are currently only two known case where this path is triggered:
  //   __interceptor_free
  //   __nptl_deallocate_tsd
  //   start_thread
  //   clone
  // and:
  //   ResetRange
  //   __interceptor_munmap
  //   __deallocate_stack
  //   start_thread
  //   clone
  // Ideally, we destroy thread state (and unwire proc) when a thread actually
  // exits (i.e. when we join/wait it). Then we would not need the global proc
  gp->mtx.Lock();
  ProcWire(gp->proc, thr);
}

ScopedGlobalProcessor::~ScopedGlobalProcessor() {
  GlobalProc *gp = global_proc();
  ThreadState *thr = cur_thread();
  if (thr->proc() != gp->proc)
    return;
  ProcUnwire(gp->proc, thr);
  gp->mtx.Unlock();
}

void AllocatorLockBeforeFork() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  global_proc()->internal_alloc_mtx.Lock();
  InternalAllocatorLock();
#if !SANITIZER_APPLE
  // OS X allocates from hooks, see 6a3958247a.
  allocator()->ForceLock();
  StackDepotLockBeforeFork();
#endif
}

void AllocatorUnlockAfterFork(bool child) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
#if !SANITIZER_APPLE
  StackDepotUnlockAfterFork(child);
  allocator()->ForceUnlock();
#endif
  InternalAllocatorUnlock();
  global_proc()->internal_alloc_mtx.Unlock();
}

void GlobalProcessorLock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  global_proc()->mtx.Lock();
}

void GlobalProcessorUnlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  global_proc()->mtx.Unlock();
}

static constexpr uptr kMaxAllowedMallocSize = 1ull << 40;
static uptr max_user_defined_malloc_size;

void InitializeAllocator() {
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator()->Init(common_flags()->allocator_release_to_os_interval_ms);
  max_user_defined_malloc_size = common_flags()->max_allocation_size_mb
                                     ? common_flags()->max_allocation_size_mb
                                           << 20
                                     : kMaxAllowedMallocSize;
}

void InitializeAllocatorLate() {
  new(global_proc()) GlobalProc();
}

void AllocatorProcStart(Processor *proc) {
  allocator()->InitCache(&proc->alloc_cache);
  internal_allocator()->InitCache(&proc->internal_alloc_cache);
}

void AllocatorProcFinish(Processor *proc) {
  allocator()->DestroyCache(&proc->alloc_cache);
  internal_allocator()->DestroyCache(&proc->internal_alloc_cache);
}

void AllocatorPrintStats() {
  allocator()->PrintStats();
}

static void SignalUnsafeCall(ThreadState *thr, uptr pc) {
  if (atomic_load_relaxed(&thr->in_signal_handler) == 0 ||
      !ShouldReport(thr, ReportTypeSignalUnsafe))
    return;
  VarSizeStackTrace stack;
  ObtainCurrentStack(thr, pc, &stack);
  if (IsFiredSuppression(ctx, ReportTypeSignalUnsafe, stack))
    return;
  ThreadRegistryLock l(&ctx->thread_registry);
  ScopedReport rep(ReportTypeSignalUnsafe);
  rep.AddStack(stack, true);
  OutputReport(thr, rep);
}


void *user_alloc_internal(ThreadState *thr, uptr pc, uptr sz, uptr align,
                          bool signal) {
  if (sz >= kMaxAllowedMallocSize || align >= kMaxAllowedMallocSize ||
      sz > max_user_defined_malloc_size) {
    if (AllocatorMayReturnNull())
      return nullptr;
    uptr malloc_limit =
        Min(kMaxAllowedMallocSize, max_user_defined_malloc_size);
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportAllocationSizeTooBig(sz, malloc_limit, &stack);
  }
  if (UNLIKELY(IsRssLimitExceeded())) {
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportRssLimitExceeded(&stack);
  }
  void *p = allocator()->Allocate(&thr->proc()->alloc_cache, sz, align);
  if (UNLIKELY(!p)) {
    SetAllocatorOutOfMemory();
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportOutOfMemory(sz, &stack);
  }
  if (ctx && ctx->initialized)
    OnUserAlloc(thr, pc, (uptr)p, sz, true);
  if (signal)
    SignalUnsafeCall(thr, pc);
  return p;
}

void user_free(ThreadState *thr, uptr pc, void *p, bool signal) {
  ScopedGlobalProcessor sgp;
  if (ctx && ctx->initialized)
    OnUserFree(thr, pc, (uptr)p, true);
  allocator()->Deallocate(&thr->proc()->alloc_cache, p);
  if (signal)
    SignalUnsafeCall(thr, pc);
}

void *user_alloc(ThreadState *thr, uptr pc, uptr sz) {
  return SetErrnoOnNull(user_alloc_internal(thr, pc, sz, kDefaultAlignment));
}

void *user_calloc(ThreadState *thr, uptr pc, uptr size, uptr n) {
  if (UNLIKELY(CheckForCallocOverflow(size, n))) {
    if (AllocatorMayReturnNull())
      return SetErrnoOnNull(nullptr);
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportCallocOverflow(n, size, &stack);
  }
  void *p = user_alloc_internal(thr, pc, n * size);
  if (p)
    internal_memset(p, 0, n * size);
  return SetErrnoOnNull(p);
}

void *user_reallocarray(ThreadState *thr, uptr pc, void *p, uptr size, uptr n) {
  if (UNLIKELY(CheckForCallocOverflow(size, n))) {
    if (AllocatorMayReturnNull())
      return SetErrnoOnNull(nullptr);
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportReallocArrayOverflow(size, n, &stack);
  }
  return user_realloc(thr, pc, p, size * n);
}

void OnUserAlloc(ThreadState *thr, uptr pc, uptr p, uptr sz, bool write) {
  DPrintf("#%d: alloc(%zu) = 0x%zx\n", thr->tid, sz, p);
  // Note: this can run before thread initialization/after finalization.
  // As a result this is not necessarily synchronized with DoReset,
  // which iterates over and resets all sync objects,
  // but it is fine to create new MBlocks in this context.
  ctx->metamap.AllocBlock(thr, pc, p, sz);
  // If this runs before thread initialization/after finalization
  // and we don't have trace initialized, we can't imitate writes.
  // In such case just reset the shadow range, it is fine since
  // it affects only a small fraction of special objects.
  if (write && thr->ignore_reads_and_writes == 0 &&
      atomic_load_relaxed(&thr->trace_pos))
    MemoryRangeImitateWrite(thr, pc, (uptr)p, sz);
  else
    MemoryResetRange(thr, pc, (uptr)p, sz);
}

void OnUserFree(ThreadState *thr, uptr pc, uptr p, bool write) {
  CHECK_NE(p, (void*)0);
  if (!thr->slot) {
    // Very early/late in thread lifetime, or during fork.
    UNUSED uptr sz = ctx->metamap.FreeBlock(thr->proc(), p, false);
    DPrintf("#%d: free(0x%zx, %zu) (no slot)\n", thr->tid, p, sz);
    return;
  }
  SlotLocker locker(thr);
  uptr sz = ctx->metamap.FreeBlock(thr->proc(), p, true);
  DPrintf("#%d: free(0x%zx, %zu)\n", thr->tid, p, sz);
  if (write && thr->ignore_reads_and_writes == 0)
    MemoryRangeFreed(thr, pc, (uptr)p, sz);
}

void *user_realloc(ThreadState *thr, uptr pc, void *p, uptr sz) {
  // FIXME: Handle "shrinking" more efficiently,
  // it seems that some software actually does this.
  if (!p)
    return SetErrnoOnNull(user_alloc_internal(thr, pc, sz));
  if (!sz) {
    user_free(thr, pc, p);
    return nullptr;
  }
  void *new_p = user_alloc_internal(thr, pc, sz);
  if (new_p) {
    uptr old_sz = user_alloc_usable_size(p);
    internal_memcpy(new_p, p, min(old_sz, sz));
    user_free(thr, pc, p);
  }
  return SetErrnoOnNull(new_p);
}

void *user_memalign(ThreadState *thr, uptr pc, uptr align, uptr sz) {
  if (UNLIKELY(!IsPowerOfTwo(align))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportInvalidAllocationAlignment(align, &stack);
  }
  return SetErrnoOnNull(user_alloc_internal(thr, pc, sz, align));
}

int user_posix_memalign(ThreadState *thr, uptr pc, void **memptr, uptr align,
                        uptr sz) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(align))) {
    if (AllocatorMayReturnNull())
      return errno_EINVAL;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportInvalidPosixMemalignAlignment(align, &stack);
  }
  void *ptr = user_alloc_internal(thr, pc, sz, align);
  if (UNLIKELY(!ptr))
    // OOM error is already taken care of by user_alloc_internal.
    return errno_ENOMEM;
  CHECK(IsAligned((uptr)ptr, align));
  *memptr = ptr;
  return 0;
}

void *user_aligned_alloc(ThreadState *thr, uptr pc, uptr align, uptr sz) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(align, sz))) {
    errno = errno_EINVAL;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportInvalidAlignedAllocAlignment(sz, align, &stack);
  }
  return SetErrnoOnNull(user_alloc_internal(thr, pc, sz, align));
}

void *user_valloc(ThreadState *thr, uptr pc, uptr sz) {
  return SetErrnoOnNull(user_alloc_internal(thr, pc, sz, GetPageSizeCached()));
}

void *user_pvalloc(ThreadState *thr, uptr pc, uptr sz) {
  uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(sz, PageSize))) {
    errno = errno_ENOMEM;
    if (AllocatorMayReturnNull())
      return nullptr;
    GET_STACK_TRACE_FATAL(thr, pc);
    ReportPvallocOverflow(sz, &stack);
  }
  // pvalloc(0) should allocate one page.
  sz = sz ? RoundUpTo(sz, PageSize) : PageSize;
  return SetErrnoOnNull(user_alloc_internal(thr, pc, sz, PageSize));
}

static const void *user_alloc_begin(const void *p) {
  if (p == nullptr || !IsAppMem((uptr)p))
    return nullptr;
  void *beg = allocator()->GetBlockBegin(p);
  if (!beg)
    return nullptr;

  MBlock *b = ctx->metamap.GetBlock((uptr)beg);
  if (!b)
    return nullptr;  // Not a valid pointer.

  return (const void *)beg;
}

uptr user_alloc_usable_size(const void *p) {
  if (p == 0 || !IsAppMem((uptr)p))
    return 0;
  MBlock *b = ctx->metamap.GetBlock((uptr)p);
  if (!b)
    return 0;  // Not a valid pointer.
  if (b->siz == 0)
    return 1;  // Zero-sized allocations are actually 1 byte.
  return b->siz;
}

uptr user_alloc_usable_size_fast(const void *p) {
  MBlock *b = ctx->metamap.GetBlock((uptr)p);
  // Static objects may have malloc'd before tsan completes
  // initialization, and may believe returned ptrs to be valid.
  if (!b)
    return 0;  // Not a valid pointer.
  if (b->siz == 0)
    return 1;  // Zero-sized allocations are actually 1 byte.
  return b->siz;
}

void invoke_malloc_hook(void *ptr, uptr size) {
  ThreadState *thr = cur_thread();
  if (ctx == 0 || !ctx->initialized || thr->ignore_interceptors)
    return;
  RunMallocHooks(ptr, size);
}

void invoke_free_hook(void *ptr) {
  ThreadState *thr = cur_thread();
  if (ctx == 0 || !ctx->initialized || thr->ignore_interceptors)
    return;
  RunFreeHooks(ptr);
}

void *Alloc(uptr sz) {
  ThreadState *thr = cur_thread();
  if (thr->nomalloc) {
    thr->nomalloc = 0;  // CHECK calls internal_malloc().
    CHECK(0);
  }
  InternalAllocAccess();
  return InternalAlloc(sz, &thr->proc()->internal_alloc_cache);
}

void FreeImpl(void *p) {
  ThreadState *thr = cur_thread();
  if (thr->nomalloc) {
    thr->nomalloc = 0;  // CHECK calls internal_malloc().
    CHECK(0);
  }
  InternalAllocAccess();
  InternalFree(p, &thr->proc()->internal_alloc_cache);
}

}  // namespace __tsan

using namespace __tsan;

extern "C" {
uptr __sanitizer_get_current_allocated_bytes() {
  uptr stats[AllocatorStatCount];
  allocator()->GetStats(stats);
  return stats[AllocatorStatAllocated];
}

uptr __sanitizer_get_heap_size() {
  uptr stats[AllocatorStatCount];
  allocator()->GetStats(stats);
  return stats[AllocatorStatMapped];
}

uptr __sanitizer_get_free_bytes() {
  return 1;
}

uptr __sanitizer_get_unmapped_bytes() {
  return 1;
}

uptr __sanitizer_get_estimated_allocated_size(uptr size) {
  return size;
}

int __sanitizer_get_ownership(const void *p) {
  return allocator()->GetBlockBegin(p) != 0;
}

const void *__sanitizer_get_allocated_begin(const void *p) {
  return user_alloc_begin(p);
}

uptr __sanitizer_get_allocated_size(const void *p) {
  return user_alloc_usable_size(p);
}

uptr __sanitizer_get_allocated_size_fast(const void *p) {
  DCHECK_EQ(p, __sanitizer_get_allocated_begin(p));
  uptr ret = user_alloc_usable_size_fast(p);
  DCHECK_EQ(ret, __sanitizer_get_allocated_size(p));
  return ret;
}

void __sanitizer_purge_allocator() {
  allocator()->ForceReleaseToOS();
}

void __tsan_on_thread_idle() {
  ThreadState *thr = cur_thread();
  allocator()->SwallowCache(&thr->proc()->alloc_cache);
  internal_allocator()->SwallowCache(&thr->proc()->internal_alloc_cache);
  ctx->metamap.OnProcIdle(thr->proc());
}
}  // extern "C"
