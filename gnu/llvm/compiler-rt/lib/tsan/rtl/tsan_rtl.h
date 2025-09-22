//===-- tsan_rtl.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Main internal TSan header file.
//
// Ground rules:
//   - C++ run-time should not be used (static CTORs, RTTI, exceptions, static
//     function-scope locals)
//   - All functions/classes/etc reside in namespace __tsan, except for those
//     declared in tsan_interface.h.
//   - Platform-specific files should be used instead of ifdefs (*).
//   - No system headers included in header files (*).
//   - Platform specific headres included only into platform-specific files (*).
//
//  (*) Except when inlining is critical for performance.
//===----------------------------------------------------------------------===//

#ifndef TSAN_RTL_H
#define TSAN_RTL_H

#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_asm.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_deadlock_detector_interface.h"
#include "sanitizer_common/sanitizer_libignore.h"
#include "sanitizer_common/sanitizer_suppressions.h"
#include "sanitizer_common/sanitizer_thread_registry.h"
#include "sanitizer_common/sanitizer_vector.h"
#include "tsan_defs.h"
#include "tsan_flags.h"
#include "tsan_ignoreset.h"
#include "tsan_ilist.h"
#include "tsan_mman.h"
#include "tsan_mutexset.h"
#include "tsan_platform.h"
#include "tsan_report.h"
#include "tsan_shadow.h"
#include "tsan_stack_trace.h"
#include "tsan_sync.h"
#include "tsan_trace.h"
#include "tsan_vector_clock.h"

#if SANITIZER_WORDSIZE != 64
# error "ThreadSanitizer is supported only on 64-bit platforms"
#endif

namespace __tsan {

#if !SANITIZER_GO
struct MapUnmapCallback;
#  if defined(__mips64) || defined(__aarch64__) || defined(__loongarch__) || \
      defined(__powerpc__) || SANITIZER_RISCV64

struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 0;
  typedef __sanitizer::CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = 20;
  using AddressSpaceView = LocalAddressSpaceView;
  typedef __tsan::MapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
typedef SizeClassAllocator32<AP32> PrimaryAllocator;
#else
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
#    if defined(__s390x__)
  typedef MappingS390x Mapping;
#    else
  typedef Mapping48AddressSpace Mapping;
#    endif
  static const uptr kSpaceBeg = Mapping::kHeapMemBeg;
  static const uptr kSpaceSize = Mapping::kHeapMemEnd - Mapping::kHeapMemBeg;
  static const uptr kMetadataSize = 0;
  typedef DefaultSizeClassMap SizeClassMap;
  typedef __tsan::MapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};
typedef SizeClassAllocator64<AP64> PrimaryAllocator;
#endif
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;
Allocator *allocator();
#endif

struct ThreadSignalContext;

struct JmpBuf {
  uptr sp;
  int int_signal_send;
  bool in_blocking_func;
  uptr in_signal_handler;
  uptr *shadow_stack_pos;
};

// A Processor represents a physical thread, or a P for Go.
// It is used to store internal resources like allocate cache, and does not
// participate in race-detection logic (invisible to end user).
// In C++ it is tied to an OS thread just like ThreadState, however ideally
// it should be tied to a CPU (this way we will have fewer allocator caches).
// In Go it is tied to a P, so there are significantly fewer Processor's than
// ThreadState's (which are tied to Gs).
// A ThreadState must be wired with a Processor to handle events.
struct Processor {
  ThreadState *thr; // currently wired thread, or nullptr
#if !SANITIZER_GO
  AllocatorCache alloc_cache;
  InternalAllocatorCache internal_alloc_cache;
#endif
  DenseSlabAllocCache block_cache;
  DenseSlabAllocCache sync_cache;
  DDPhysicalThread *dd_pt;
};

#if !SANITIZER_GO
// ScopedGlobalProcessor temporary setups a global processor for the current
// thread, if it does not have one. Intended for interceptors that can run
// at the very thread end, when we already destroyed the thread processor.
struct ScopedGlobalProcessor {
  ScopedGlobalProcessor();
  ~ScopedGlobalProcessor();
};
#endif

struct TidEpoch {
  Tid tid;
  Epoch epoch;
};

struct alignas(SANITIZER_CACHE_LINE_SIZE) TidSlot {
  Mutex mtx;
  Sid sid;
  atomic_uint32_t raw_epoch;
  ThreadState *thr;
  Vector<TidEpoch> journal;
  INode node;

  Epoch epoch() const {
    return static_cast<Epoch>(atomic_load(&raw_epoch, memory_order_relaxed));
  }

  void SetEpoch(Epoch v) {
    atomic_store(&raw_epoch, static_cast<u32>(v), memory_order_relaxed);
  }

  TidSlot();
};

// This struct is stored in TLS.
struct alignas(SANITIZER_CACHE_LINE_SIZE) ThreadState {
  FastState fast_state;
  int ignore_sync;
#if !SANITIZER_GO
  int ignore_interceptors;
#endif
  uptr *shadow_stack_pos;

  // Current position in tctx->trace.Back()->events (Event*).
  atomic_uintptr_t trace_pos;
  // PC of the last memory access, used to compute PC deltas in the trace.
  uptr trace_prev_pc;

  // Technically `current` should be a separate THREADLOCAL variable;
  // but it is placed here in order to share cache line with previous fields.
  ThreadState* current;

  atomic_sint32_t pending_signals;

  VectorClock clock;

  // This is a slow path flag. On fast path, fast_state.GetIgnoreBit() is read.
  // We do not distinguish beteween ignoring reads and writes
  // for better performance.
  int ignore_reads_and_writes;
  int suppress_reports;
  // Go does not support ignores.
#if !SANITIZER_GO
  IgnoreSet mop_ignore_set;
  IgnoreSet sync_ignore_set;
#endif
  uptr *shadow_stack;
  uptr *shadow_stack_end;
#if !SANITIZER_GO
  Vector<JmpBuf> jmp_bufs;
  int in_symbolizer;
  atomic_uintptr_t in_blocking_func;
  bool in_ignored_lib;
  bool is_inited;
#endif
  MutexSet mset;
  bool is_dead;
  const Tid tid;
  uptr stk_addr;
  uptr stk_size;
  uptr tls_addr;
  uptr tls_size;
  ThreadContext *tctx;

  DDLogicalThread *dd_lt;

  TidSlot *slot;
  uptr slot_epoch;
  bool slot_locked;

  // Current wired Processor, or nullptr. Required to handle any events.
  Processor *proc1;
#if !SANITIZER_GO
  Processor *proc() { return proc1; }
#else
  Processor *proc();
#endif

  atomic_uintptr_t in_signal_handler;
  atomic_uintptr_t signal_ctx;

#if !SANITIZER_GO
  StackID last_sleep_stack_id;
  VectorClock last_sleep_clock;
#endif

  // Set in regions of runtime that must be signal-safe and fork-safe.
  // If set, malloc must not be called.
  int nomalloc;

  const ReportDesc *current_report;

  explicit ThreadState(Tid tid);
};

#if !SANITIZER_GO
#if SANITIZER_APPLE || SANITIZER_ANDROID
ThreadState *cur_thread();
void set_cur_thread(ThreadState *thr);
void cur_thread_finalize();
inline ThreadState *cur_thread_init() { return cur_thread(); }
#  else
__attribute__((tls_model("initial-exec")))
extern THREADLOCAL char cur_thread_placeholder[];
inline ThreadState *cur_thread() {
  return reinterpret_cast<ThreadState *>(cur_thread_placeholder)->current;
}
inline ThreadState *cur_thread_init() {
  ThreadState *thr = reinterpret_cast<ThreadState *>(cur_thread_placeholder);
  if (UNLIKELY(!thr->current))
    thr->current = thr;
  return thr->current;
}
inline void set_cur_thread(ThreadState *thr) {
  reinterpret_cast<ThreadState *>(cur_thread_placeholder)->current = thr;
}
inline void cur_thread_finalize() { }
#  endif  // SANITIZER_APPLE || SANITIZER_ANDROID
#endif  // SANITIZER_GO

class ThreadContext final : public ThreadContextBase {
 public:
  explicit ThreadContext(Tid tid);
  ~ThreadContext();
  ThreadState *thr;
  StackID creation_stack_id;
  VectorClock *sync;
  uptr sync_epoch;
  Trace trace;

  // Override superclass callbacks.
  void OnDead() override;
  void OnJoined(void *arg) override;
  void OnFinished() override;
  void OnStarted(void *arg) override;
  void OnCreated(void *arg) override;
  void OnReset() override;
  void OnDetached(void *arg) override;
};

struct RacyStacks {
  MD5Hash hash[2];
  bool operator==(const RacyStacks &other) const;
};

struct RacyAddress {
  uptr addr_min;
  uptr addr_max;
};

struct FiredSuppression {
  ReportType type;
  uptr pc_or_addr;
  Suppression *supp;
};

struct Context {
  Context();

  bool initialized;
#if !SANITIZER_GO
  bool after_multithreaded_fork;
#endif

  MetaMap metamap;

  Mutex report_mtx;
  int nreported;
  atomic_uint64_t last_symbolize_time_ns;

  void *background_thread;
  atomic_uint32_t stop_background_thread;

  ThreadRegistry thread_registry;

  // This is used to prevent a very unlikely but very pathological behavior.
  // Since memory access handling is not synchronized with DoReset,
  // a thread running concurrently with DoReset can leave a bogus shadow value
  // that will be later falsely detected as a race. For such false races
  // RestoreStack will return false and we will not report it.
  // However, consider that a thread leaves a whole lot of such bogus values
  // and these values are later read by a whole lot of threads.
  // This will cause massive amounts of ReportRace calls and lots of
  // serialization. In very pathological cases the resulting slowdown
  // can be >100x. This is very unlikely, but it was presumably observed
  // in practice: https://github.com/google/sanitizers/issues/1552
  // If this happens, previous access sid+epoch will be the same for all of
  // these false races b/c if the thread will try to increment epoch, it will
  // notice that DoReset has happened and will stop producing bogus shadow
  // values. So, last_spurious_race is used to remember the last sid+epoch
  // for which RestoreStack returned false. Then it is used to filter out
  // races with the same sid+epoch very early and quickly.
  // It is of course possible that multiple threads left multiple bogus shadow
  // values and all of them are read by lots of threads at the same time.
  // In such case last_spurious_race will only be able to deduplicate a few
  // races from one thread, then few from another and so on. An alternative
  // would be to hold an array of such sid+epoch, but we consider such scenario
  // as even less likely.
  // Note: this can lead to some rare false negatives as well:
  // 1. When a legit access with the same sid+epoch participates in a race
  // as the "previous" memory access, it will be wrongly filtered out.
  // 2. When RestoreStack returns false for a legit memory access because it
  // was already evicted from the thread trace, we will still remember it in
  // last_spurious_race. Then if there is another racing memory access from
  // the same thread that happened in the same epoch, but was stored in the
  // next thread trace part (which is still preserved in the thread trace),
  // we will also wrongly filter it out while RestoreStack would actually
  // succeed for that second memory access.
  RawShadow last_spurious_race;

  Mutex racy_mtx;
  Vector<RacyStacks> racy_stacks;
  // Number of fired suppressions may be large enough.
  Mutex fired_suppressions_mtx;
  InternalMmapVector<FiredSuppression> fired_suppressions;
  DDetector *dd;

  Flags flags;
  fd_t memprof_fd;

  // The last slot index (kFreeSid) is used to denote freed memory.
  TidSlot slots[kThreadSlotCount - 1];

  // Protects global_epoch, slot_queue, trace_part_recycle.
  Mutex slot_mtx;
  uptr global_epoch;  // guarded by slot_mtx and by all slot mutexes
  bool resetting;     // global reset is in progress
  IList<TidSlot, &TidSlot::node> slot_queue SANITIZER_GUARDED_BY(slot_mtx);
  IList<TraceHeader, &TraceHeader::global, TracePart> trace_part_recycle
      SANITIZER_GUARDED_BY(slot_mtx);
  uptr trace_part_total_allocated SANITIZER_GUARDED_BY(slot_mtx);
  uptr trace_part_recycle_finished SANITIZER_GUARDED_BY(slot_mtx);
  uptr trace_part_finished_excess SANITIZER_GUARDED_BY(slot_mtx);
#if SANITIZER_GO
  uptr mapped_shadow_begin;
  uptr mapped_shadow_end;
#endif
};

extern Context *ctx;  // The one and the only global runtime context.

ALWAYS_INLINE Flags *flags() {
  return &ctx->flags;
}

struct ScopedIgnoreInterceptors {
  ScopedIgnoreInterceptors() {
#if !SANITIZER_GO
    cur_thread()->ignore_interceptors++;
#endif
  }

  ~ScopedIgnoreInterceptors() {
#if !SANITIZER_GO
    cur_thread()->ignore_interceptors--;
#endif
  }
};

const char *GetObjectTypeFromTag(uptr tag);
const char *GetReportHeaderFromTag(uptr tag);
uptr TagFromShadowStackFrame(uptr pc);

class ScopedReportBase {
 public:
  void AddMemoryAccess(uptr addr, uptr external_tag, Shadow s, Tid tid,
                       StackTrace stack, const MutexSet *mset);
  void AddStack(StackTrace stack, bool suppressable = false);
  void AddThread(const ThreadContext *tctx, bool suppressable = false);
  void AddThread(Tid tid, bool suppressable = false);
  void AddUniqueTid(Tid unique_tid);
  int AddMutex(uptr addr, StackID creation_stack_id);
  void AddLocation(uptr addr, uptr size);
  void AddSleep(StackID stack_id);
  void SetCount(int count);
  void SetSigNum(int sig);

  const ReportDesc *GetReport() const;

 protected:
  ScopedReportBase(ReportType typ, uptr tag);
  ~ScopedReportBase();

 private:
  ReportDesc *rep_;
  // Symbolizer makes lots of intercepted calls. If we try to process them,
  // at best it will cause deadlocks on internal mutexes.
  ScopedIgnoreInterceptors ignore_interceptors_;

  ScopedReportBase(const ScopedReportBase &) = delete;
  void operator=(const ScopedReportBase &) = delete;
};

class ScopedReport : public ScopedReportBase {
 public:
  explicit ScopedReport(ReportType typ, uptr tag = kExternalTagNone);
  ~ScopedReport();

 private:
  ScopedErrorReportLock lock_;
};

bool ShouldReport(ThreadState *thr, ReportType typ);
ThreadContext *IsThreadStackOrTls(uptr addr, bool *is_stack);

// The stack could look like:
//   <start> | <main> | <foo> | tag | <bar>
// This will extract the tag and keep:
//   <start> | <main> | <foo> | <bar>
template<typename StackTraceTy>
void ExtractTagFromStack(StackTraceTy *stack, uptr *tag = nullptr) {
  if (stack->size < 2) return;
  uptr possible_tag_pc = stack->trace[stack->size - 2];
  uptr possible_tag = TagFromShadowStackFrame(possible_tag_pc);
  if (possible_tag == kExternalTagNone) return;
  stack->trace_buffer[stack->size - 2] = stack->trace_buffer[stack->size - 1];
  stack->size -= 1;
  if (tag) *tag = possible_tag;
}

template<typename StackTraceTy>
void ObtainCurrentStack(ThreadState *thr, uptr toppc, StackTraceTy *stack,
                        uptr *tag = nullptr) {
  uptr size = thr->shadow_stack_pos - thr->shadow_stack;
  uptr start = 0;
  if (size + !!toppc > kStackTraceMax) {
    start = size + !!toppc - kStackTraceMax;
    size = kStackTraceMax - !!toppc;
  }
  stack->Init(&thr->shadow_stack[start], size, toppc);
  ExtractTagFromStack(stack, tag);
}

#define GET_STACK_TRACE_FATAL(thr, pc) \
  VarSizeStackTrace stack; \
  ObtainCurrentStack(thr, pc, &stack); \
  stack.ReverseOrder();

void MapShadow(uptr addr, uptr size);
void MapThreadTrace(uptr addr, uptr size, const char *name);
void DontNeedShadowFor(uptr addr, uptr size);
void UnmapShadow(ThreadState *thr, uptr addr, uptr size);
void InitializeShadowMemory();
void DontDumpShadow(uptr addr, uptr size);
void InitializeInterceptors();
void InitializeLibIgnore();
void InitializeDynamicAnnotations();

void ForkBefore(ThreadState *thr, uptr pc);
void ForkParentAfter(ThreadState *thr, uptr pc);
void ForkChildAfter(ThreadState *thr, uptr pc, bool start_thread);

void ReportRace(ThreadState *thr, RawShadow *shadow_mem, Shadow cur, Shadow old,
                AccessType typ);
bool OutputReport(ThreadState *thr, const ScopedReport &srep);
bool IsFiredSuppression(Context *ctx, ReportType type, StackTrace trace);
bool IsExpectedReport(uptr addr, uptr size);

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 1
# define DPrintf Printf
#else
# define DPrintf(...)
#endif

#if defined(TSAN_DEBUG_OUTPUT) && TSAN_DEBUG_OUTPUT >= 2
# define DPrintf2 Printf
#else
# define DPrintf2(...)
#endif

StackID CurrentStackId(ThreadState *thr, uptr pc);
ReportStack *SymbolizeStackId(StackID stack_id);
void PrintCurrentStack(ThreadState *thr, uptr pc);
void PrintCurrentStackSlow(uptr pc);  // uses libunwind
MBlock *JavaHeapBlock(uptr addr, uptr *start);

void Initialize(ThreadState *thr);
void MaybeSpawnBackgroundThread();
int Finalize(ThreadState *thr);

void OnUserAlloc(ThreadState *thr, uptr pc, uptr p, uptr sz, bool write);
void OnUserFree(ThreadState *thr, uptr pc, uptr p, bool write);

void MemoryAccess(ThreadState *thr, uptr pc, uptr addr, uptr size,
                  AccessType typ);
void UnalignedMemoryAccess(ThreadState *thr, uptr pc, uptr addr, uptr size,
                           AccessType typ);
// This creates 2 non-inlined specialized versions of MemoryAccessRange.
template <bool is_read>
void MemoryAccessRangeT(ThreadState *thr, uptr pc, uptr addr, uptr size);

ALWAYS_INLINE
void MemoryAccessRange(ThreadState *thr, uptr pc, uptr addr, uptr size,
                       bool is_write) {
  if (size == 0)
    return;
  if (is_write)
    MemoryAccessRangeT<false>(thr, pc, addr, size);
  else
    MemoryAccessRangeT<true>(thr, pc, addr, size);
}

void ShadowSet(RawShadow *p, RawShadow *end, RawShadow v);
void MemoryRangeFreed(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryResetRange(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryRangeImitateWrite(ThreadState *thr, uptr pc, uptr addr, uptr size);
void MemoryRangeImitateWriteOrResetRange(ThreadState *thr, uptr pc, uptr addr,
                                         uptr size);

void ThreadIgnoreBegin(ThreadState *thr, uptr pc);
void ThreadIgnoreEnd(ThreadState *thr);
void ThreadIgnoreSyncBegin(ThreadState *thr, uptr pc);
void ThreadIgnoreSyncEnd(ThreadState *thr);

Tid ThreadCreate(ThreadState *thr, uptr pc, uptr uid, bool detached);
void ThreadStart(ThreadState *thr, Tid tid, tid_t os_id,
                 ThreadType thread_type);
void ThreadFinish(ThreadState *thr);
Tid ThreadConsumeTid(ThreadState *thr, uptr pc, uptr uid);
void ThreadJoin(ThreadState *thr, uptr pc, Tid tid);
void ThreadDetach(ThreadState *thr, uptr pc, Tid tid);
void ThreadFinalize(ThreadState *thr);
void ThreadSetName(ThreadState *thr, const char *name);
int ThreadCount(ThreadState *thr);
void ProcessPendingSignalsImpl(ThreadState *thr);
void ThreadNotJoined(ThreadState *thr, uptr pc, Tid tid, uptr uid);

Processor *ProcCreate();
void ProcDestroy(Processor *proc);
void ProcWire(Processor *proc, ThreadState *thr);
void ProcUnwire(Processor *proc, ThreadState *thr);

// Note: the parameter is called flagz, because flags is already taken
// by the global function that returns flags.
void MutexCreate(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexDestroy(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPreLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPostLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0,
    int rec = 1);
int  MutexUnlock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPreReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexPostReadLock(ThreadState *thr, uptr pc, uptr addr, u32 flagz = 0);
void MutexReadUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexReadOrWriteUnlock(ThreadState *thr, uptr pc, uptr addr);
void MutexRepair(ThreadState *thr, uptr pc, uptr addr);  // call on EOWNERDEAD
void MutexInvalidAccess(ThreadState *thr, uptr pc, uptr addr);

void Acquire(ThreadState *thr, uptr pc, uptr addr);
// AcquireGlobal synchronizes the current thread with all other threads.
// In terms of happens-before relation, it draws a HB edge from all threads
// (where they happen to execute right now) to the current thread. We use it to
// handle Go finalizers. Namely, finalizer goroutine executes AcquireGlobal
// right before executing finalizers. This provides a coarse, but simple
// approximation of the actual required synchronization.
void AcquireGlobal(ThreadState *thr);
void Release(ThreadState *thr, uptr pc, uptr addr);
void ReleaseStoreAcquire(ThreadState *thr, uptr pc, uptr addr);
void ReleaseStore(ThreadState *thr, uptr pc, uptr addr);
void AfterSleep(ThreadState *thr, uptr pc);
void IncrementEpoch(ThreadState *thr);

#if !SANITIZER_GO
uptr ALWAYS_INLINE HeapEnd() {
  return HeapMemEnd() + PrimaryAllocator::AdditionalSize();
}
#endif

void SlotAttachAndLock(ThreadState *thr) SANITIZER_ACQUIRE(thr->slot->mtx);
void SlotDetach(ThreadState *thr);
void SlotLock(ThreadState *thr) SANITIZER_ACQUIRE(thr->slot->mtx);
void SlotUnlock(ThreadState *thr) SANITIZER_RELEASE(thr->slot->mtx);
void DoReset(ThreadState *thr, uptr epoch);
void FlushShadowMemory();

ThreadState *FiberCreate(ThreadState *thr, uptr pc, unsigned flags);
void FiberDestroy(ThreadState *thr, uptr pc, ThreadState *fiber);
void FiberSwitch(ThreadState *thr, uptr pc, ThreadState *fiber, unsigned flags);

// These need to match __tsan_switch_to_fiber_* flags defined in
// tsan_interface.h. See documentation there as well.
enum FiberSwitchFlags {
  FiberSwitchFlagNoSync = 1 << 0, // __tsan_switch_to_fiber_no_sync
};

class SlotLocker {
 public:
  ALWAYS_INLINE
  SlotLocker(ThreadState *thr, bool recursive = false)
      : thr_(thr), locked_(recursive ? thr->slot_locked : false) {
#if !SANITIZER_GO
    // We are in trouble if we are here with in_blocking_func set.
    // If in_blocking_func is set, all signals will be delivered synchronously,
    // which means we can't lock slots since the signal handler will try
    // to lock it recursively and deadlock.
    DCHECK(!atomic_load(&thr->in_blocking_func, memory_order_relaxed));
#endif
    if (!locked_)
      SlotLock(thr_);
  }

  ALWAYS_INLINE
  ~SlotLocker() {
    if (!locked_)
      SlotUnlock(thr_);
  }

 private:
  ThreadState *thr_;
  bool locked_;
};

class SlotUnlocker {
 public:
  SlotUnlocker(ThreadState *thr) : thr_(thr), locked_(thr->slot_locked) {
    if (locked_)
      SlotUnlock(thr_);
  }

  ~SlotUnlocker() {
    if (locked_)
      SlotLock(thr_);
  }

 private:
  ThreadState *thr_;
  bool locked_;
};

ALWAYS_INLINE void ProcessPendingSignals(ThreadState *thr) {
  if (UNLIKELY(atomic_load_relaxed(&thr->pending_signals)))
    ProcessPendingSignalsImpl(thr);
}

extern bool is_initialized;

ALWAYS_INLINE
void LazyInitialize(ThreadState *thr) {
  // If we can use .preinit_array, assume that __tsan_init
  // called from .preinit_array initializes runtime before
  // any instrumented code except when tsan is used as a 
  // shared library.
#if (!SANITIZER_CAN_USE_PREINIT_ARRAY || defined(SANITIZER_SHARED))
  if (UNLIKELY(!is_initialized))
    Initialize(thr);
#endif
}

void TraceResetForTesting();
void TraceSwitchPart(ThreadState *thr);
void TraceSwitchPartImpl(ThreadState *thr);
bool RestoreStack(EventType type, Sid sid, Epoch epoch, uptr addr, uptr size,
                  AccessType typ, Tid *ptid, VarSizeStackTrace *pstk,
                  MutexSet *pmset, uptr *ptag);

template <typename EventT>
ALWAYS_INLINE WARN_UNUSED_RESULT bool TraceAcquire(ThreadState *thr,
                                                   EventT **ev) {
  // TraceSwitchPart accesses shadow_stack, but it's called infrequently,
  // so we check it here proactively.
  DCHECK(thr->shadow_stack);
  Event *pos = reinterpret_cast<Event *>(atomic_load_relaxed(&thr->trace_pos));
#if SANITIZER_DEBUG
  // TraceSwitch acquires these mutexes,
  // so we lock them here to detect deadlocks more reliably.
  { Lock lock(&ctx->slot_mtx); }
  { Lock lock(&thr->tctx->trace.mtx); }
  TracePart *current = thr->tctx->trace.parts.Back();
  if (current) {
    DCHECK_GE(pos, &current->events[0]);
    DCHECK_LE(pos, &current->events[TracePart::kSize]);
  } else {
    DCHECK_EQ(pos, nullptr);
  }
#endif
  // TracePart is allocated with mmap and is at least 4K aligned.
  // So the following check is a faster way to check for part end.
  // It may have false positives in the middle of the trace,
  // they are filtered out in TraceSwitch.
  if (UNLIKELY(((uptr)(pos + 1) & TracePart::kAlignment) == 0))
    return false;
  *ev = reinterpret_cast<EventT *>(pos);
  return true;
}

template <typename EventT>
ALWAYS_INLINE void TraceRelease(ThreadState *thr, EventT *evp) {
  DCHECK_LE(evp + 1, &thr->tctx->trace.parts.Back()->events[TracePart::kSize]);
  atomic_store_relaxed(&thr->trace_pos, (uptr)(evp + 1));
}

template <typename EventT>
void TraceEvent(ThreadState *thr, EventT ev) {
  EventT *evp;
  if (!TraceAcquire(thr, &evp)) {
    TraceSwitchPart(thr);
    UNUSED bool res = TraceAcquire(thr, &evp);
    DCHECK(res);
  }
  *evp = ev;
  TraceRelease(thr, evp);
}

ALWAYS_INLINE WARN_UNUSED_RESULT bool TryTraceFunc(ThreadState *thr,
                                                   uptr pc = 0) {
  if (!kCollectHistory)
    return true;
  EventFunc *ev;
  if (UNLIKELY(!TraceAcquire(thr, &ev)))
    return false;
  ev->is_access = 0;
  ev->is_func = 1;
  ev->pc = pc;
  TraceRelease(thr, ev);
  return true;
}

WARN_UNUSED_RESULT
bool TryTraceMemoryAccess(ThreadState *thr, uptr pc, uptr addr, uptr size,
                          AccessType typ);
WARN_UNUSED_RESULT
bool TryTraceMemoryAccessRange(ThreadState *thr, uptr pc, uptr addr, uptr size,
                               AccessType typ);
void TraceMemoryAccessRange(ThreadState *thr, uptr pc, uptr addr, uptr size,
                            AccessType typ);
void TraceFunc(ThreadState *thr, uptr pc = 0);
void TraceMutexLock(ThreadState *thr, EventType type, uptr pc, uptr addr,
                    StackID stk);
void TraceMutexUnlock(ThreadState *thr, uptr addr);
void TraceTime(ThreadState *thr);

void TraceRestartFuncExit(ThreadState *thr);
void TraceRestartFuncEntry(ThreadState *thr, uptr pc);

void GrowShadowStack(ThreadState *thr);

ALWAYS_INLINE
void FuncEntry(ThreadState *thr, uptr pc) {
  DPrintf2("#%d: FuncEntry %p\n", (int)thr->fast_state.sid(), (void *)pc);
  if (UNLIKELY(!TryTraceFunc(thr, pc)))
    return TraceRestartFuncEntry(thr, pc);
  DCHECK_GE(thr->shadow_stack_pos, thr->shadow_stack);
#if !SANITIZER_GO
  DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#else
  if (thr->shadow_stack_pos == thr->shadow_stack_end)
    GrowShadowStack(thr);
#endif
  thr->shadow_stack_pos[0] = pc;
  thr->shadow_stack_pos++;
}

ALWAYS_INLINE
void FuncExit(ThreadState *thr) {
  DPrintf2("#%d: FuncExit\n", (int)thr->fast_state.sid());
  if (UNLIKELY(!TryTraceFunc(thr, 0)))
    return TraceRestartFuncExit(thr);
  DCHECK_GT(thr->shadow_stack_pos, thr->shadow_stack);
#if !SANITIZER_GO
  DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#endif
  thr->shadow_stack_pos--;
}

#if !SANITIZER_GO
extern void (*on_initialize)(void);
extern int (*on_finalize)(int);
#endif
}  // namespace __tsan

#endif  // TSAN_RTL_H
