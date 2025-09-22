//===-- tsan_rtl.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Main file (entry points) for the TSan run-time.
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "tsan_defs.h"
#include "tsan_interface.h"
#include "tsan_mman.h"
#include "tsan_platform.h"
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "ubsan/ubsan_init.h"

volatile int __tsan_resumed = 0;

extern "C" void __tsan_resume() {
  __tsan_resumed = 1;
}

#if SANITIZER_APPLE
SANITIZER_WEAK_DEFAULT_IMPL
void __tsan_test_only_on_fork() {}
#endif

namespace __tsan {

#if !SANITIZER_GO
void (*on_initialize)(void);
int (*on_finalize)(int);
#endif

#if !SANITIZER_GO && !SANITIZER_APPLE
alignas(SANITIZER_CACHE_LINE_SIZE) THREADLOCAL __attribute__((tls_model(
    "initial-exec"))) char cur_thread_placeholder[sizeof(ThreadState)];
#endif
alignas(SANITIZER_CACHE_LINE_SIZE) static char ctx_placeholder[sizeof(Context)];
Context *ctx;

// Can be overriden by a front-end.
#ifdef TSAN_EXTERNAL_HOOKS
bool OnFinalize(bool failed);
void OnInitialize();
#else
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool OnFinalize(bool failed) {
#  if !SANITIZER_GO
  if (on_finalize)
    return on_finalize(failed);
#  endif
  return failed;
}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnInitialize() {
#  if !SANITIZER_GO
  if (on_initialize)
    on_initialize();
#  endif
}
#endif

static TracePart* TracePartAlloc(ThreadState* thr) {
  TracePart* part = nullptr;
  {
    Lock lock(&ctx->slot_mtx);
    uptr max_parts = Trace::kMinParts + flags()->history_size;
    Trace* trace = &thr->tctx->trace;
    if (trace->parts_allocated == max_parts ||
        ctx->trace_part_finished_excess) {
      part = ctx->trace_part_recycle.PopFront();
      DPrintf("#%d: TracePartAlloc: part=%p\n", thr->tid, part);
      if (part && part->trace) {
        Trace* trace1 = part->trace;
        Lock trace_lock(&trace1->mtx);
        part->trace = nullptr;
        TracePart* part1 = trace1->parts.PopFront();
        CHECK_EQ(part, part1);
        if (trace1->parts_allocated > trace1->parts.Size()) {
          ctx->trace_part_finished_excess +=
              trace1->parts_allocated - trace1->parts.Size();
          trace1->parts_allocated = trace1->parts.Size();
        }
      }
    }
    if (trace->parts_allocated < max_parts) {
      trace->parts_allocated++;
      if (ctx->trace_part_finished_excess)
        ctx->trace_part_finished_excess--;
    }
    if (!part)
      ctx->trace_part_total_allocated++;
    else if (ctx->trace_part_recycle_finished)
      ctx->trace_part_recycle_finished--;
  }
  if (!part)
    part = new (MmapOrDie(sizeof(*part), "TracePart")) TracePart();
  return part;
}

static void TracePartFree(TracePart* part) SANITIZER_REQUIRES(ctx->slot_mtx) {
  DCHECK(part->trace);
  part->trace = nullptr;
  ctx->trace_part_recycle.PushFront(part);
}

void TraceResetForTesting() {
  Lock lock(&ctx->slot_mtx);
  while (auto* part = ctx->trace_part_recycle.PopFront()) {
    if (auto trace = part->trace)
      CHECK_EQ(trace->parts.PopFront(), part);
    UnmapOrDie(part, sizeof(*part));
  }
  ctx->trace_part_total_allocated = 0;
  ctx->trace_part_recycle_finished = 0;
  ctx->trace_part_finished_excess = 0;
}

static void DoResetImpl(uptr epoch) {
  ThreadRegistryLock lock0(&ctx->thread_registry);
  Lock lock1(&ctx->slot_mtx);
  CHECK_EQ(ctx->global_epoch, epoch);
  ctx->global_epoch++;
  CHECK(!ctx->resetting);
  ctx->resetting = true;
  for (u32 i = ctx->thread_registry.NumThreadsLocked(); i--;) {
    ThreadContext* tctx = (ThreadContext*)ctx->thread_registry.GetThreadLocked(
        static_cast<Tid>(i));
    // Potentially we could purge all ThreadStatusDead threads from the
    // registry. Since we reset all shadow, they can't race with anything
    // anymore. However, their tid's can still be stored in some aux places
    // (e.g. tid of thread that created something).
    auto trace = &tctx->trace;
    Lock lock(&trace->mtx);
    bool attached = tctx->thr && tctx->thr->slot;
    auto parts = &trace->parts;
    bool local = false;
    while (!parts->Empty()) {
      auto part = parts->Front();
      local = local || part == trace->local_head;
      if (local)
        CHECK(!ctx->trace_part_recycle.Queued(part));
      else
        ctx->trace_part_recycle.Remove(part);
      if (attached && parts->Size() == 1) {
        // The thread is running and this is the last/current part.
        // Set the trace position to the end of the current part
        // to force the thread to call SwitchTracePart and re-attach
        // to a new slot and allocate a new trace part.
        // Note: the thread is concurrently modifying the position as well,
        // so this is only best-effort. The thread can only modify position
        // within this part, because switching parts is protected by
        // slot/trace mutexes that we hold here.
        atomic_store_relaxed(
            &tctx->thr->trace_pos,
            reinterpret_cast<uptr>(&part->events[TracePart::kSize]));
        break;
      }
      parts->Remove(part);
      TracePartFree(part);
    }
    CHECK_LE(parts->Size(), 1);
    trace->local_head = parts->Front();
    if (tctx->thr && !tctx->thr->slot) {
      atomic_store_relaxed(&tctx->thr->trace_pos, 0);
      tctx->thr->trace_prev_pc = 0;
    }
    if (trace->parts_allocated > trace->parts.Size()) {
      ctx->trace_part_finished_excess +=
          trace->parts_allocated - trace->parts.Size();
      trace->parts_allocated = trace->parts.Size();
    }
  }
  while (ctx->slot_queue.PopFront()) {
  }
  for (auto& slot : ctx->slots) {
    slot.SetEpoch(kEpochZero);
    slot.journal.Reset();
    slot.thr = nullptr;
    ctx->slot_queue.PushBack(&slot);
  }

  DPrintf("Resetting shadow...\n");
  auto shadow_begin = ShadowBeg();
  auto shadow_end = ShadowEnd();
#if SANITIZER_GO
  CHECK_NE(0, ctx->mapped_shadow_begin);
  shadow_begin = ctx->mapped_shadow_begin;
  shadow_end = ctx->mapped_shadow_end;
  VPrintf(2, "shadow_begin-shadow_end: (0x%zx-0x%zx)\n",
          shadow_begin, shadow_end);
#endif

#if SANITIZER_WINDOWS
  auto resetFailed =
      !ZeroMmapFixedRegion(shadow_begin, shadow_end - shadow_begin);
#else
  auto resetFailed =
      !MmapFixedSuperNoReserve(shadow_begin, shadow_end-shadow_begin, "shadow");
#  if !SANITIZER_GO
  DontDumpShadow(shadow_begin, shadow_end - shadow_begin);
#  endif
#endif
  if (resetFailed) {
    Printf("failed to reset shadow memory\n");
    Die();
  }
  DPrintf("Resetting meta shadow...\n");
  ctx->metamap.ResetClocks();
  StoreShadow(&ctx->last_spurious_race, Shadow::kEmpty);
  ctx->resetting = false;
}

// Clang does not understand locking all slots in the loop:
// error: expecting mutex 'slot.mtx' to be held at start of each loop
void DoReset(ThreadState* thr, uptr epoch) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  for (auto& slot : ctx->slots) {
    slot.mtx.Lock();
    if (UNLIKELY(epoch == 0))
      epoch = ctx->global_epoch;
    if (UNLIKELY(epoch != ctx->global_epoch)) {
      // Epoch can't change once we've locked the first slot.
      CHECK_EQ(slot.sid, 0);
      slot.mtx.Unlock();
      return;
    }
  }
  DPrintf("#%d: DoReset epoch=%lu\n", thr ? thr->tid : -1, epoch);
  DoResetImpl(epoch);
  for (auto& slot : ctx->slots) slot.mtx.Unlock();
}

void FlushShadowMemory() { DoReset(nullptr, 0); }

static TidSlot* FindSlotAndLock(ThreadState* thr)
    SANITIZER_ACQUIRE(thr->slot->mtx) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  CHECK(!thr->slot);
  TidSlot* slot = nullptr;
  for (;;) {
    uptr epoch;
    {
      Lock lock(&ctx->slot_mtx);
      epoch = ctx->global_epoch;
      if (slot) {
        // This is an exhausted slot from the previous iteration.
        if (ctx->slot_queue.Queued(slot))
          ctx->slot_queue.Remove(slot);
        thr->slot_locked = false;
        slot->mtx.Unlock();
      }
      for (;;) {
        slot = ctx->slot_queue.PopFront();
        if (!slot)
          break;
        if (slot->epoch() != kEpochLast) {
          ctx->slot_queue.PushBack(slot);
          break;
        }
      }
    }
    if (!slot) {
      DoReset(thr, epoch);
      continue;
    }
    slot->mtx.Lock();
    CHECK(!thr->slot_locked);
    thr->slot_locked = true;
    if (slot->thr) {
      DPrintf("#%d: preempting sid=%d tid=%d\n", thr->tid, (u32)slot->sid,
              slot->thr->tid);
      slot->SetEpoch(slot->thr->fast_state.epoch());
      slot->thr = nullptr;
    }
    if (slot->epoch() != kEpochLast)
      return slot;
  }
}

void SlotAttachAndLock(ThreadState* thr) {
  TidSlot* slot = FindSlotAndLock(thr);
  DPrintf("#%d: SlotAttach: slot=%u\n", thr->tid, static_cast<int>(slot->sid));
  CHECK(!slot->thr);
  CHECK(!thr->slot);
  slot->thr = thr;
  thr->slot = slot;
  Epoch epoch = EpochInc(slot->epoch());
  CHECK(!EpochOverflow(epoch));
  slot->SetEpoch(epoch);
  thr->fast_state.SetSid(slot->sid);
  thr->fast_state.SetEpoch(epoch);
  if (thr->slot_epoch != ctx->global_epoch) {
    thr->slot_epoch = ctx->global_epoch;
    thr->clock.Reset();
#if !SANITIZER_GO
    thr->last_sleep_stack_id = kInvalidStackID;
    thr->last_sleep_clock.Reset();
#endif
  }
  thr->clock.Set(slot->sid, epoch);
  slot->journal.PushBack({thr->tid, epoch});
}

static void SlotDetachImpl(ThreadState* thr, bool exiting) {
  TidSlot* slot = thr->slot;
  thr->slot = nullptr;
  if (thr != slot->thr) {
    slot = nullptr;  // we don't own the slot anymore
    if (thr->slot_epoch != ctx->global_epoch) {
      TracePart* part = nullptr;
      auto* trace = &thr->tctx->trace;
      {
        Lock l(&trace->mtx);
        auto* parts = &trace->parts;
        // The trace can be completely empty in an unlikely event
        // the thread is preempted right after it acquired the slot
        // in ThreadStart and did not trace any events yet.
        CHECK_LE(parts->Size(), 1);
        part = parts->PopFront();
        thr->tctx->trace.local_head = nullptr;
        atomic_store_relaxed(&thr->trace_pos, 0);
        thr->trace_prev_pc = 0;
      }
      if (part) {
        Lock l(&ctx->slot_mtx);
        TracePartFree(part);
      }
    }
    return;
  }
  CHECK(exiting || thr->fast_state.epoch() == kEpochLast);
  slot->SetEpoch(thr->fast_state.epoch());
  slot->thr = nullptr;
}

void SlotDetach(ThreadState* thr) {
  Lock lock(&thr->slot->mtx);
  SlotDetachImpl(thr, true);
}

void SlotLock(ThreadState* thr) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  DCHECK(!thr->slot_locked);
#if SANITIZER_DEBUG
  // Check these mutexes are not locked.
  // We can call DoReset from SlotAttachAndLock, which will lock
  // these mutexes, but it happens only every once in a while.
  { ThreadRegistryLock lock(&ctx->thread_registry); }
  { Lock lock(&ctx->slot_mtx); }
#endif
  TidSlot* slot = thr->slot;
  slot->mtx.Lock();
  thr->slot_locked = true;
  if (LIKELY(thr == slot->thr && thr->fast_state.epoch() != kEpochLast))
    return;
  SlotDetachImpl(thr, false);
  thr->slot_locked = false;
  slot->mtx.Unlock();
  SlotAttachAndLock(thr);
}

void SlotUnlock(ThreadState* thr) {
  DCHECK(thr->slot_locked);
  thr->slot_locked = false;
  thr->slot->mtx.Unlock();
}

Context::Context()
    : initialized(),
      report_mtx(MutexTypeReport),
      nreported(),
      thread_registry([](Tid tid) -> ThreadContextBase* {
        return new (Alloc(sizeof(ThreadContext))) ThreadContext(tid);
      }),
      racy_mtx(MutexTypeRacy),
      racy_stacks(),
      fired_suppressions_mtx(MutexTypeFired),
      slot_mtx(MutexTypeSlots),
      resetting() {
  fired_suppressions.reserve(8);
  for (uptr i = 0; i < ARRAY_SIZE(slots); i++) {
    TidSlot* slot = &slots[i];
    slot->sid = static_cast<Sid>(i);
    slot_queue.PushBack(slot);
  }
  global_epoch = 1;
}

TidSlot::TidSlot() : mtx(MutexTypeSlot) {}

// The objects are allocated in TLS, so one may rely on zero-initialization.
ThreadState::ThreadState(Tid tid)
    // Do not touch these, rely on zero initialization,
    // they may be accessed before the ctor.
    // ignore_reads_and_writes()
    // ignore_interceptors()
    : tid(tid) {
  CHECK_EQ(reinterpret_cast<uptr>(this) % SANITIZER_CACHE_LINE_SIZE, 0);
#if !SANITIZER_GO
  // C/C++ uses fixed size shadow stack.
  const int kInitStackSize = kShadowStackSize;
  shadow_stack = static_cast<uptr*>(
      MmapNoReserveOrDie(kInitStackSize * sizeof(uptr), "shadow stack"));
  SetShadowRegionHugePageMode(reinterpret_cast<uptr>(shadow_stack),
                              kInitStackSize * sizeof(uptr));
#else
  // Go uses malloc-allocated shadow stack with dynamic size.
  const int kInitStackSize = 8;
  shadow_stack = static_cast<uptr*>(Alloc(kInitStackSize * sizeof(uptr)));
#endif
  shadow_stack_pos = shadow_stack;
  shadow_stack_end = shadow_stack + kInitStackSize;
}

#if !SANITIZER_GO
void MemoryProfiler(u64 uptime) {
  if (ctx->memprof_fd == kInvalidFd)
    return;
  InternalMmapVector<char> buf(4096);
  WriteMemoryProfile(buf.data(), buf.size(), uptime);
  WriteToFile(ctx->memprof_fd, buf.data(), internal_strlen(buf.data()));
}

static bool InitializeMemoryProfiler() {
  ctx->memprof_fd = kInvalidFd;
  const char *fname = flags()->profile_memory;
  if (!fname || !fname[0])
    return false;
  if (internal_strcmp(fname, "stdout") == 0) {
    ctx->memprof_fd = 1;
  } else if (internal_strcmp(fname, "stderr") == 0) {
    ctx->memprof_fd = 2;
  } else {
    InternalScopedString filename;
    filename.AppendF("%s.%d", fname, (int)internal_getpid());
    ctx->memprof_fd = OpenFile(filename.data(), WrOnly);
    if (ctx->memprof_fd == kInvalidFd) {
      Printf("ThreadSanitizer: failed to open memory profile file '%s'\n",
             filename.data());
      return false;
    }
  }
  MemoryProfiler(0);
  return true;
}

static void *BackgroundThread(void *arg) {
  // This is a non-initialized non-user thread, nothing to see here.
  // We don't use ScopedIgnoreInterceptors, because we want ignores to be
  // enabled even when the thread function exits (e.g. during pthread thread
  // shutdown code).
  cur_thread_init()->ignore_interceptors++;
  const u64 kMs2Ns = 1000 * 1000;
  const u64 start = NanoTime();

  u64 last_flush = start;
  uptr last_rss = 0;
  while (!atomic_load_relaxed(&ctx->stop_background_thread)) {
    SleepForMillis(100);
    u64 now = NanoTime();

    // Flush memory if requested.
    if (flags()->flush_memory_ms > 0) {
      if (last_flush + flags()->flush_memory_ms * kMs2Ns < now) {
        VReport(1, "ThreadSanitizer: periodic memory flush\n");
        FlushShadowMemory();
        now = last_flush = NanoTime();
      }
    }
    if (flags()->memory_limit_mb > 0) {
      uptr rss = GetRSS();
      uptr limit = uptr(flags()->memory_limit_mb) << 20;
      VReport(1,
              "ThreadSanitizer: memory flush check"
              " RSS=%llu LAST=%llu LIMIT=%llu\n",
              (u64)rss >> 20, (u64)last_rss >> 20, (u64)limit >> 20);
      if (2 * rss > limit + last_rss) {
        VReport(1, "ThreadSanitizer: flushing memory due to RSS\n");
        FlushShadowMemory();
        rss = GetRSS();
        now = NanoTime();
        VReport(1, "ThreadSanitizer: memory flushed RSS=%llu\n",
                (u64)rss >> 20);
      }
      last_rss = rss;
    }

    MemoryProfiler(now - start);

    // Flush symbolizer cache if requested.
    if (flags()->flush_symbolizer_ms > 0) {
      u64 last = atomic_load(&ctx->last_symbolize_time_ns,
                             memory_order_relaxed);
      if (last != 0 && last + flags()->flush_symbolizer_ms * kMs2Ns < now) {
        Lock l(&ctx->report_mtx);
        ScopedErrorReportLock l2;
        SymbolizeFlush();
        atomic_store(&ctx->last_symbolize_time_ns, 0, memory_order_relaxed);
      }
    }
  }
  return nullptr;
}

static void StartBackgroundThread() {
  ctx->background_thread = internal_start_thread(&BackgroundThread, 0);
}

#ifndef __mips__
static void StopBackgroundThread() {
  atomic_store(&ctx->stop_background_thread, 1, memory_order_relaxed);
  internal_join_thread(ctx->background_thread);
  ctx->background_thread = 0;
}
#endif
#endif

void DontNeedShadowFor(uptr addr, uptr size) {
  ReleaseMemoryPagesToOS(reinterpret_cast<uptr>(MemToShadow(addr)),
                         reinterpret_cast<uptr>(MemToShadow(addr + size)));
}

#if !SANITIZER_GO
// We call UnmapShadow before the actual munmap, at that point we don't yet
// know if the provided address/size are sane. We can't call UnmapShadow
// after the actual munmap becuase at that point the memory range can
// already be reused for something else, so we can't rely on the munmap
// return value to understand is the values are sane.
// While calling munmap with insane values (non-canonical address, negative
// size, etc) is an error, the kernel won't crash. We must also try to not
// crash as the failure mode is very confusing (paging fault inside of the
// runtime on some derived shadow address).
static bool IsValidMmapRange(uptr addr, uptr size) {
  if (size == 0)
    return true;
  if (static_cast<sptr>(size) < 0)
    return false;
  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return false;
  // Check that if the start of the region belongs to one of app ranges,
  // end of the region belongs to the same region.
  const uptr ranges[][2] = {
      {LoAppMemBeg(), LoAppMemEnd()},
      {MidAppMemBeg(), MidAppMemEnd()},
      {HiAppMemBeg(), HiAppMemEnd()},
  };
  for (auto range : ranges) {
    if (addr >= range[0] && addr < range[1])
      return addr + size <= range[1];
  }
  return false;
}

void UnmapShadow(ThreadState *thr, uptr addr, uptr size) {
  if (size == 0 || !IsValidMmapRange(addr, size))
    return;
  DontNeedShadowFor(addr, size);
  ScopedGlobalProcessor sgp;
  SlotLocker locker(thr, true);
  ctx->metamap.ResetRange(thr->proc(), addr, size, true);
}
#endif

void MapShadow(uptr addr, uptr size) {
  // Ensure thead registry lock held, so as to synchronize
  // with DoReset, which also access the mapped_shadow_* ctxt fields.
  ThreadRegistryLock lock0(&ctx->thread_registry);
  static bool data_mapped = false;

#if !SANITIZER_GO
  // Global data is not 64K aligned, but there are no adjacent mappings,
  // so we can get away with unaligned mapping.
  // CHECK_EQ(addr, addr & ~((64 << 10) - 1));  // windows wants 64K alignment
  const uptr kPageSize = GetPageSizeCached();
  uptr shadow_begin = RoundDownTo((uptr)MemToShadow(addr), kPageSize);
  uptr shadow_end = RoundUpTo((uptr)MemToShadow(addr + size), kPageSize);
  if (!MmapFixedNoReserve(shadow_begin, shadow_end - shadow_begin, "shadow"))
    Die();
#else
  uptr shadow_begin = RoundDownTo((uptr)MemToShadow(addr), (64 << 10));
  uptr shadow_end = RoundUpTo((uptr)MemToShadow(addr + size), (64 << 10));
  VPrintf(2, "MapShadow for (0x%zx-0x%zx), begin/end: (0x%zx-0x%zx)\n",
          addr, addr + size, shadow_begin, shadow_end);

  if (!data_mapped) {
    // First call maps data+bss.
    if (!MmapFixedSuperNoReserve(shadow_begin, shadow_end - shadow_begin, "shadow"))
      Die();
  } else {
    VPrintf(2, "ctx->mapped_shadow_{begin,end} = (0x%zx-0x%zx)\n",
            ctx->mapped_shadow_begin, ctx->mapped_shadow_end);
    // Second and subsequent calls map heap.
    if (shadow_end <= ctx->mapped_shadow_end)
      return;
    if (!ctx->mapped_shadow_begin || ctx->mapped_shadow_begin > shadow_begin)
       ctx->mapped_shadow_begin = shadow_begin;
    if (shadow_begin < ctx->mapped_shadow_end)
      shadow_begin = ctx->mapped_shadow_end;
    VPrintf(2, "MapShadow begin/end = (0x%zx-0x%zx)\n",
            shadow_begin, shadow_end);
    if (!MmapFixedSuperNoReserve(shadow_begin, shadow_end - shadow_begin,
                                 "shadow"))
      Die();
    ctx->mapped_shadow_end = shadow_end;
  }
#endif

  // Meta shadow is 2:1, so tread carefully.
  static uptr mapped_meta_end = 0;
  uptr meta_begin = (uptr)MemToMeta(addr);
  uptr meta_end = (uptr)MemToMeta(addr + size);
  meta_begin = RoundDownTo(meta_begin, 64 << 10);
  meta_end = RoundUpTo(meta_end, 64 << 10);
  if (!data_mapped) {
    // First call maps data+bss.
    data_mapped = true;
    if (!MmapFixedSuperNoReserve(meta_begin, meta_end - meta_begin,
                                 "meta shadow"))
      Die();
  } else {
    // Mapping continuous heap.
    // Windows wants 64K alignment.
    meta_begin = RoundDownTo(meta_begin, 64 << 10);
    meta_end = RoundUpTo(meta_end, 64 << 10);
    CHECK_GT(meta_end, mapped_meta_end);
    if (meta_begin < mapped_meta_end)
      meta_begin = mapped_meta_end;
    if (!MmapFixedSuperNoReserve(meta_begin, meta_end - meta_begin,
                                 "meta shadow"))
      Die();
    mapped_meta_end = meta_end;
  }
  VPrintf(2, "mapped meta shadow for (0x%zx-0x%zx) at (0x%zx-0x%zx)\n", addr,
          addr + size, meta_begin, meta_end);
}

#if !SANITIZER_GO
static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  stack->Unwind(StackTrace::GetNextInstructionPc(sig.pc), sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

static void TsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetTid(), &OnStackUnwind, nullptr);
}
#endif

void CheckUnwind() {
  // There is high probability that interceptors will check-fail as well,
  // on the other hand there is no sense in processing interceptors
  // since we are going to die soon.
  ScopedIgnoreInterceptors ignore;
#if !SANITIZER_GO
  ThreadState* thr = cur_thread();
  thr->nomalloc = false;
  thr->ignore_sync++;
  thr->ignore_reads_and_writes++;
  atomic_store_relaxed(&thr->in_signal_handler, 0);
#endif
  PrintCurrentStackSlow(StackTrace::GetCurrentPc());
}

bool is_initialized;

void Initialize(ThreadState *thr) {
  // Thread safe because done before all threads exist.
  if (is_initialized)
    return;
  is_initialized = true;
  // We are not ready to handle interceptors yet.
  ScopedIgnoreInterceptors ignore;
  SanitizerToolName = "ThreadSanitizer";
  // Install tool-specific callbacks in sanitizer_common.
  SetCheckUnwindCallback(CheckUnwind);

  ctx = new(ctx_placeholder) Context;
  const char *env_name = SANITIZER_GO ? "GORACE" : "TSAN_OPTIONS";
  const char *options = GetEnv(env_name);
  CacheBinaryName();
  CheckASLR();
  InitializeFlags(&ctx->flags, options, env_name);
  AvoidCVE_2016_2143();
  __sanitizer::InitializePlatformEarly();
  __tsan::InitializePlatformEarly();

#if !SANITIZER_GO
  InitializeAllocator();
  ReplaceSystemMalloc();
#endif
  if (common_flags()->detect_deadlocks)
    ctx->dd = DDetector::Create(flags());
  Processor *proc = ProcCreate();
  ProcWire(proc, thr);
  InitializeInterceptors();
  InitializePlatform();
  InitializeDynamicAnnotations();
#if !SANITIZER_GO
  InitializeShadowMemory();
  InitializeAllocatorLate();
  InstallDeadlySignalHandlers(TsanOnDeadlySignal);
#endif
  // Setup correct file descriptor for error reports.
  __sanitizer_set_report_path(common_flags()->log_path);
  InitializeSuppressions();
#if !SANITIZER_GO
  InitializeLibIgnore();
  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);
#endif

  VPrintf(1, "***** Running under ThreadSanitizer v3 (pid %d) *****\n",
          (int)internal_getpid());

  // Initialize thread 0.
  Tid tid = ThreadCreate(nullptr, 0, 0, true);
  CHECK_EQ(tid, kMainTid);
  ThreadStart(thr, tid, GetTid(), ThreadType::Regular);
#if TSAN_CONTAINS_UBSAN
  __ubsan::InitAsPlugin();
#endif

#if !SANITIZER_GO
  Symbolizer::LateInitialize();
  if (InitializeMemoryProfiler() || flags()->force_background_thread)
    MaybeSpawnBackgroundThread();
#endif
  ctx->initialized = true;

  if (flags()->stop_on_start) {
    Printf("ThreadSanitizer is suspended at startup (pid %d)."
           " Call __tsan_resume().\n",
           (int)internal_getpid());
    while (__tsan_resumed == 0) {}
  }

  OnInitialize();
}

void MaybeSpawnBackgroundThread() {
  // On MIPS, TSan initialization is run before
  // __pthread_initialize_minimal_internal() is finished, so we can not spawn
  // new threads.
#if !SANITIZER_GO && !defined(__mips__)
  static atomic_uint32_t bg_thread = {};
  if (atomic_load(&bg_thread, memory_order_relaxed) == 0 &&
      atomic_exchange(&bg_thread, 1, memory_order_relaxed) == 0) {
    StartBackgroundThread();
    SetSandboxingCallback(StopBackgroundThread);
  }
#endif
}

int Finalize(ThreadState *thr) {
  bool failed = false;

#if !SANITIZER_GO
  if (common_flags()->print_module_map == 1)
    DumpProcessMap();
#endif

  if (flags()->atexit_sleep_ms > 0 && ThreadCount(thr) > 1)
    internal_usleep(u64(flags()->atexit_sleep_ms) * 1000);

  {
    // Wait for pending reports.
    ScopedErrorReportLock lock;
  }

#if !SANITIZER_GO
  if (Verbosity()) AllocatorPrintStats();
#endif

  ThreadFinalize(thr);

  if (ctx->nreported) {
    failed = true;
#if !SANITIZER_GO
    Printf("ThreadSanitizer: reported %d warnings\n", ctx->nreported);
#else
    Printf("Found %d data race(s)\n", ctx->nreported);
#endif
  }

  if (common_flags()->print_suppressions)
    PrintMatchedSuppressions();

  failed = OnFinalize(failed);

  return failed ? common_flags()->exitcode : 0;
}

#if !SANITIZER_GO
void ForkBefore(ThreadState* thr, uptr pc) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  GlobalProcessorLock();
  // Detaching from the slot makes OnUserFree skip writing to the shadow.
  // The slot will be locked so any attempts to use it will deadlock anyway.
  SlotDetach(thr);
  for (auto& slot : ctx->slots) slot.mtx.Lock();
  ctx->thread_registry.Lock();
  ctx->slot_mtx.Lock();
  ScopedErrorReportLock::Lock();
  AllocatorLockBeforeFork();
  // Suppress all reports in the pthread_atfork callbacks.
  // Reports will deadlock on the report_mtx.
  // We could ignore sync operations as well,
  // but so far it's unclear if it will do more good or harm.
  // Unnecessarily ignoring things can lead to false positives later.
  thr->suppress_reports++;
  // On OS X, REAL(fork) can call intercepted functions (OSSpinLockLock), and
  // we'll assert in CheckNoLocks() unless we ignore interceptors.
  // On OS X libSystem_atfork_prepare/parent/child callbacks are called
  // after/before our callbacks and they call free.
  thr->ignore_interceptors++;
  // Disables memory write in OnUserAlloc/Free.
  thr->ignore_reads_and_writes++;

#  if SANITIZER_APPLE
  __tsan_test_only_on_fork();
#  endif
}

static void ForkAfter(ThreadState* thr,
                      bool child) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  thr->suppress_reports--;  // Enabled in ForkBefore.
  thr->ignore_interceptors--;
  thr->ignore_reads_and_writes--;
  AllocatorUnlockAfterFork(child);
  ScopedErrorReportLock::Unlock();
  ctx->slot_mtx.Unlock();
  ctx->thread_registry.Unlock();
  for (auto& slot : ctx->slots) slot.mtx.Unlock();
  SlotAttachAndLock(thr);
  SlotUnlock(thr);
  GlobalProcessorUnlock();
}

void ForkParentAfter(ThreadState* thr, uptr pc) { ForkAfter(thr, false); }

void ForkChildAfter(ThreadState* thr, uptr pc, bool start_thread) {
  ForkAfter(thr, true);
  u32 nthread = ctx->thread_registry.OnFork(thr->tid);
  VPrintf(1,
          "ThreadSanitizer: forked new process with pid %d,"
          " parent had %d threads\n",
          (int)internal_getpid(), (int)nthread);
  if (nthread == 1) {
    if (start_thread)
      StartBackgroundThread();
  } else {
    // We've just forked a multi-threaded process. We cannot reasonably function
    // after that (some mutexes may be locked before fork). So just enable
    // ignores for everything in the hope that we will exec soon.
    ctx->after_multithreaded_fork = true;
    thr->ignore_interceptors++;
    thr->suppress_reports++;
    ThreadIgnoreBegin(thr, pc);
    ThreadIgnoreSyncBegin(thr, pc);
  }
}
#endif

#if SANITIZER_GO
NOINLINE
void GrowShadowStack(ThreadState *thr) {
  const int sz = thr->shadow_stack_end - thr->shadow_stack;
  const int newsz = 2 * sz;
  auto *newstack = (uptr *)Alloc(newsz * sizeof(uptr));
  internal_memcpy(newstack, thr->shadow_stack, sz * sizeof(uptr));
  Free(thr->shadow_stack);
  thr->shadow_stack = newstack;
  thr->shadow_stack_pos = newstack + sz;
  thr->shadow_stack_end = newstack + newsz;
}
#endif

StackID CurrentStackId(ThreadState *thr, uptr pc) {
#if !SANITIZER_GO
  if (!thr->is_inited)  // May happen during bootstrap.
    return kInvalidStackID;
#endif
  if (pc != 0) {
#if !SANITIZER_GO
    DCHECK_LT(thr->shadow_stack_pos, thr->shadow_stack_end);
#else
    if (thr->shadow_stack_pos == thr->shadow_stack_end)
      GrowShadowStack(thr);
#endif
    thr->shadow_stack_pos[0] = pc;
    thr->shadow_stack_pos++;
  }
  StackID id = StackDepotPut(
      StackTrace(thr->shadow_stack, thr->shadow_stack_pos - thr->shadow_stack));
  if (pc != 0)
    thr->shadow_stack_pos--;
  return id;
}

static bool TraceSkipGap(ThreadState* thr) {
  Trace *trace = &thr->tctx->trace;
  Event *pos = reinterpret_cast<Event *>(atomic_load_relaxed(&thr->trace_pos));
  DCHECK_EQ(reinterpret_cast<uptr>(pos + 1) & TracePart::kAlignment, 0);
  auto *part = trace->parts.Back();
  DPrintf("#%d: TraceSwitchPart enter trace=%p parts=%p-%p pos=%p\n", thr->tid,
          trace, trace->parts.Front(), part, pos);
  if (!part)
    return false;
  // We can get here when we still have space in the current trace part.
  // The fast-path check in TraceAcquire has false positives in the middle of
  // the part. Check if we are indeed at the end of the current part or not,
  // and fill any gaps with NopEvent's.
  Event* end = &part->events[TracePart::kSize];
  DCHECK_GE(pos, &part->events[0]);
  DCHECK_LE(pos, end);
  if (pos + 1 < end) {
    if ((reinterpret_cast<uptr>(pos) & TracePart::kAlignment) ==
        TracePart::kAlignment)
      *pos++ = NopEvent;
    *pos++ = NopEvent;
    DCHECK_LE(pos + 2, end);
    atomic_store_relaxed(&thr->trace_pos, reinterpret_cast<uptr>(pos));
    return true;
  }
  // We are indeed at the end.
  for (; pos < end; pos++) *pos = NopEvent;
  return false;
}

NOINLINE
void TraceSwitchPart(ThreadState* thr) {
  if (TraceSkipGap(thr))
    return;
#if !SANITIZER_GO
  if (ctx->after_multithreaded_fork) {
    // We just need to survive till exec.
    TracePart* part = thr->tctx->trace.parts.Back();
    if (part) {
      atomic_store_relaxed(&thr->trace_pos,
                           reinterpret_cast<uptr>(&part->events[0]));
      return;
    }
  }
#endif
  TraceSwitchPartImpl(thr);
}

void TraceSwitchPartImpl(ThreadState* thr) {
  SlotLocker locker(thr, true);
  Trace* trace = &thr->tctx->trace;
  TracePart* part = TracePartAlloc(thr);
  part->trace = trace;
  thr->trace_prev_pc = 0;
  TracePart* recycle = nullptr;
  // Keep roughly half of parts local to the thread
  // (not queued into the recycle queue).
  uptr local_parts = (Trace::kMinParts + flags()->history_size + 1) / 2;
  {
    Lock lock(&trace->mtx);
    if (trace->parts.Empty())
      trace->local_head = part;
    if (trace->parts.Size() >= local_parts) {
      recycle = trace->local_head;
      trace->local_head = trace->parts.Next(recycle);
    }
    trace->parts.PushBack(part);
    atomic_store_relaxed(&thr->trace_pos,
                         reinterpret_cast<uptr>(&part->events[0]));
  }
  // Make this part self-sufficient by restoring the current stack
  // and mutex set in the beginning of the trace.
  TraceTime(thr);
  {
    // Pathologically large stacks may not fit into the part.
    // In these cases we log only fixed number of top frames.
    const uptr kMaxFrames = 1000;
    // Check that kMaxFrames won't consume the whole part.
    static_assert(kMaxFrames < TracePart::kSize / 2, "kMaxFrames is too big");
    uptr* pos = Max(&thr->shadow_stack[0], thr->shadow_stack_pos - kMaxFrames);
    for (; pos < thr->shadow_stack_pos; pos++) {
      if (TryTraceFunc(thr, *pos))
        continue;
      CHECK(TraceSkipGap(thr));
      CHECK(TryTraceFunc(thr, *pos));
    }
  }
  for (uptr i = 0; i < thr->mset.Size(); i++) {
    MutexSet::Desc d = thr->mset.Get(i);
    for (uptr i = 0; i < d.count; i++)
      TraceMutexLock(thr, d.write ? EventType::kLock : EventType::kRLock, 0,
                     d.addr, d.stack_id);
  }
  // Callers of TraceSwitchPart expect that TraceAcquire will always succeed
  // after the call. It's possible that TryTraceFunc/TraceMutexLock above
  // filled the trace part exactly up to the TracePart::kAlignment gap
  // and the next TraceAcquire won't succeed. Skip the gap to avoid that.
  EventFunc *ev;
  if (!TraceAcquire(thr, &ev)) {
    CHECK(TraceSkipGap(thr));
    CHECK(TraceAcquire(thr, &ev));
  }
  {
    Lock lock(&ctx->slot_mtx);
    // There is a small chance that the slot may be not queued at this point.
    // This can happen if the slot has kEpochLast epoch and another thread
    // in FindSlotAndLock discovered that it's exhausted and removed it from
    // the slot queue. kEpochLast can happen in 2 cases: (1) if TraceSwitchPart
    // was called with the slot locked and epoch already at kEpochLast,
    // or (2) if we've acquired a new slot in SlotLock in the beginning
    // of the function and the slot was at kEpochLast - 1, so after increment
    // in SlotAttachAndLock it become kEpochLast.
    if (ctx->slot_queue.Queued(thr->slot)) {
      ctx->slot_queue.Remove(thr->slot);
      ctx->slot_queue.PushBack(thr->slot);
    }
    if (recycle)
      ctx->trace_part_recycle.PushBack(recycle);
  }
  DPrintf("#%d: TraceSwitchPart exit parts=%p-%p pos=0x%zx\n", thr->tid,
          trace->parts.Front(), trace->parts.Back(),
          atomic_load_relaxed(&thr->trace_pos));
}

void ThreadIgnoreBegin(ThreadState* thr, uptr pc) {
  DPrintf("#%d: ThreadIgnoreBegin\n", thr->tid);
  thr->ignore_reads_and_writes++;
  CHECK_GT(thr->ignore_reads_and_writes, 0);
  thr->fast_state.SetIgnoreBit();
#if !SANITIZER_GO
  if (pc && !ctx->after_multithreaded_fork)
    thr->mop_ignore_set.Add(CurrentStackId(thr, pc));
#endif
}

void ThreadIgnoreEnd(ThreadState *thr) {
  DPrintf("#%d: ThreadIgnoreEnd\n", thr->tid);
  CHECK_GT(thr->ignore_reads_and_writes, 0);
  thr->ignore_reads_and_writes--;
  if (thr->ignore_reads_and_writes == 0) {
    thr->fast_state.ClearIgnoreBit();
#if !SANITIZER_GO
    thr->mop_ignore_set.Reset();
#endif
  }
}

#if !SANITIZER_GO
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
uptr __tsan_testonly_shadow_stack_current_size() {
  ThreadState *thr = cur_thread();
  return thr->shadow_stack_pos - thr->shadow_stack;
}
#endif

void ThreadIgnoreSyncBegin(ThreadState *thr, uptr pc) {
  DPrintf("#%d: ThreadIgnoreSyncBegin\n", thr->tid);
  thr->ignore_sync++;
  CHECK_GT(thr->ignore_sync, 0);
#if !SANITIZER_GO
  if (pc && !ctx->after_multithreaded_fork)
    thr->sync_ignore_set.Add(CurrentStackId(thr, pc));
#endif
}

void ThreadIgnoreSyncEnd(ThreadState *thr) {
  DPrintf("#%d: ThreadIgnoreSyncEnd\n", thr->tid);
  CHECK_GT(thr->ignore_sync, 0);
  thr->ignore_sync--;
#if !SANITIZER_GO
  if (thr->ignore_sync == 0)
    thr->sync_ignore_set.Reset();
#endif
}

bool MD5Hash::operator==(const MD5Hash &other) const {
  return hash[0] == other.hash[0] && hash[1] == other.hash[1];
}

#if SANITIZER_DEBUG
void build_consistency_debug() {}
#else
void build_consistency_release() {}
#endif
}  // namespace __tsan

#if SANITIZER_CHECK_DEADLOCKS
namespace __sanitizer {
using namespace __tsan;
MutexMeta mutex_meta[] = {
    {MutexInvalid, "Invalid", {}},
    {MutexThreadRegistry,
     "ThreadRegistry",
     {MutexTypeSlots, MutexTypeTrace, MutexTypeReport}},
    {MutexTypeReport, "Report", {MutexTypeTrace}},
    {MutexTypeSyncVar, "SyncVar", {MutexTypeReport, MutexTypeTrace}},
    {MutexTypeAnnotations, "Annotations", {}},
    {MutexTypeAtExit, "AtExit", {}},
    {MutexTypeFired, "Fired", {MutexLeaf}},
    {MutexTypeRacy, "Racy", {MutexLeaf}},
    {MutexTypeGlobalProc, "GlobalProc", {MutexTypeSlot, MutexTypeSlots}},
    {MutexTypeInternalAlloc, "InternalAlloc", {MutexLeaf}},
    {MutexTypeTrace, "Trace", {}},
    {MutexTypeSlot,
     "Slot",
     {MutexMulti, MutexTypeTrace, MutexTypeSyncVar, MutexThreadRegistry,
      MutexTypeSlots}},
    {MutexTypeSlots, "Slots", {MutexTypeTrace, MutexTypeReport}},
    {},
};

void PrintMutexPC(uptr pc) { StackTrace(&pc, 1).Print(); }

}  // namespace __sanitizer
#endif
