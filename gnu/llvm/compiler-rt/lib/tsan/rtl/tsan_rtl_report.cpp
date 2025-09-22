//===-- tsan_rtl_report.cpp -----------------------------------------------===//
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

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "tsan_fd.h"
#include "tsan_flags.h"
#include "tsan_mman.h"
#include "tsan_platform.h"
#include "tsan_report.h"
#include "tsan_rtl.h"
#include "tsan_suppressions.h"
#include "tsan_symbolize.h"
#include "tsan_sync.h"

namespace __tsan {

using namespace __sanitizer;

static ReportStack *SymbolizeStack(StackTrace trace);

// Can be overriden by an application/test to intercept reports.
#ifdef TSAN_EXTERNAL_HOOKS
bool OnReport(const ReportDesc *rep, bool suppressed);
#else
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool OnReport(const ReportDesc *rep, bool suppressed) {
  (void)rep;
  return suppressed;
}
#endif

SANITIZER_WEAK_DEFAULT_IMPL
void __tsan_on_report(const ReportDesc *rep) {
  (void)rep;
}

static void StackStripMain(SymbolizedStack *frames) {
  SymbolizedStack *last_frame = nullptr;
  SymbolizedStack *last_frame2 = nullptr;
  for (SymbolizedStack *cur = frames; cur; cur = cur->next) {
    last_frame2 = last_frame;
    last_frame = cur;
  }

  if (last_frame2 == 0)
    return;
#if !SANITIZER_GO
  const char *last = last_frame->info.function;
  const char *last2 = last_frame2->info.function;
  // Strip frame above 'main'
  if (last2 && 0 == internal_strcmp(last2, "main")) {
    last_frame->ClearAll();
    last_frame2->next = nullptr;
  // Strip our internal thread start routine.
  } else if (last && 0 == internal_strcmp(last, "__tsan_thread_start_func")) {
    last_frame->ClearAll();
    last_frame2->next = nullptr;
    // Strip global ctors init, .preinit_array and main caller.
  } else if (last && (0 == internal_strcmp(last, "__do_global_ctors_aux") ||
                      0 == internal_strcmp(last, "__libc_csu_init") ||
                      0 == internal_strcmp(last, "__libc_start_main"))) {
    last_frame->ClearAll();
    last_frame2->next = nullptr;
  // If both are 0, then we probably just failed to symbolize.
  } else if (last || last2) {
    // Ensure that we recovered stack completely. Trimmed stack
    // can actually happen if we do not instrument some code,
    // so it's only a debug print. However we must try hard to not miss it
    // due to our fault.
    DPrintf("Bottom stack frame is missed\n");
  }
#else
  // The last frame always point into runtime (gosched0, goexit0, runtime.main).
  last_frame->ClearAll();
  last_frame2->next = nullptr;
#endif
}

ReportStack *SymbolizeStackId(u32 stack_id) {
  if (stack_id == 0)
    return 0;
  StackTrace stack = StackDepotGet(stack_id);
  if (stack.trace == nullptr)
    return nullptr;
  return SymbolizeStack(stack);
}

static ReportStack *SymbolizeStack(StackTrace trace) {
  if (trace.size == 0)
    return 0;
  SymbolizedStack *top = nullptr;
  for (uptr si = 0; si < trace.size; si++) {
    const uptr pc = trace.trace[si];
    uptr pc1 = pc;
    // We obtain the return address, but we're interested in the previous
    // instruction.
    if ((pc & kExternalPCBit) == 0)
      pc1 = StackTrace::GetPreviousInstructionPc(pc);
    SymbolizedStack *ent = SymbolizeCode(pc1);
    CHECK_NE(ent, 0);
    SymbolizedStack *last = ent;
    while (last->next) {
      last->info.address = pc;  // restore original pc for report
      last = last->next;
    }
    last->info.address = pc;  // restore original pc for report
    last->next = top;
    top = ent;
  }
  StackStripMain(top);

  auto *stack = New<ReportStack>();
  stack->frames = top;
  return stack;
}

bool ShouldReport(ThreadState *thr, ReportType typ) {
  // We set thr->suppress_reports in the fork context.
  // Taking any locking in the fork context can lead to deadlocks.
  // If any locks are already taken, it's too late to do this check.
  CheckedMutex::CheckNoLocks();
  // For the same reason check we didn't lock thread_registry yet.
  if (SANITIZER_DEBUG)
    ThreadRegistryLock l(&ctx->thread_registry);
  if (!flags()->report_bugs || thr->suppress_reports)
    return false;
  switch (typ) {
    case ReportTypeSignalUnsafe:
      return flags()->report_signal_unsafe;
    case ReportTypeThreadLeak:
#if !SANITIZER_GO
      // It's impossible to join phantom threads
      // in the child after fork.
      if (ctx->after_multithreaded_fork)
        return false;
#endif
      return flags()->report_thread_leaks;
    case ReportTypeMutexDestroyLocked:
      return flags()->report_destroy_locked;
    default:
      return true;
  }
}

ScopedReportBase::ScopedReportBase(ReportType typ, uptr tag) {
  ctx->thread_registry.CheckLocked();
  rep_ = New<ReportDesc>();
  rep_->typ = typ;
  rep_->tag = tag;
  ctx->report_mtx.Lock();
}

ScopedReportBase::~ScopedReportBase() {
  ctx->report_mtx.Unlock();
  DestroyAndFree(rep_);
}

void ScopedReportBase::AddStack(StackTrace stack, bool suppressable) {
  ReportStack **rs = rep_->stacks.PushBack();
  *rs = SymbolizeStack(stack);
  (*rs)->suppressable = suppressable;
}

void ScopedReportBase::AddMemoryAccess(uptr addr, uptr external_tag, Shadow s,
                                       Tid tid, StackTrace stack,
                                       const MutexSet *mset) {
  uptr addr0, size;
  AccessType typ;
  s.GetAccess(&addr0, &size, &typ);
  auto *mop = New<ReportMop>();
  rep_->mops.PushBack(mop);
  mop->tid = tid;
  mop->addr = addr + addr0;
  mop->size = size;
  mop->write = !(typ & kAccessRead);
  mop->atomic = typ & kAccessAtomic;
  mop->stack = SymbolizeStack(stack);
  mop->external_tag = external_tag;
  if (mop->stack)
    mop->stack->suppressable = true;
  for (uptr i = 0; i < mset->Size(); i++) {
    MutexSet::Desc d = mset->Get(i);
    int id = this->AddMutex(d.addr, d.stack_id);
    ReportMopMutex mtx = {id, d.write};
    mop->mset.PushBack(mtx);
  }
}

void ScopedReportBase::AddUniqueTid(Tid unique_tid) {
  rep_->unique_tids.PushBack(unique_tid);
}

void ScopedReportBase::AddThread(const ThreadContext *tctx, bool suppressable) {
  for (uptr i = 0; i < rep_->threads.Size(); i++) {
    if ((u32)rep_->threads[i]->id == tctx->tid)
      return;
  }
  auto *rt = New<ReportThread>();
  rep_->threads.PushBack(rt);
  rt->id = tctx->tid;
  rt->os_id = tctx->os_id;
  rt->running = (tctx->status == ThreadStatusRunning);
  rt->name = internal_strdup(tctx->name);
  rt->parent_tid = tctx->parent_tid;
  rt->thread_type = tctx->thread_type;
  rt->stack = 0;
  rt->stack = SymbolizeStackId(tctx->creation_stack_id);
  if (rt->stack)
    rt->stack->suppressable = suppressable;
}

#if !SANITIZER_GO
static ThreadContext *FindThreadByTidLocked(Tid tid) {
  ctx->thread_registry.CheckLocked();
  return static_cast<ThreadContext *>(
      ctx->thread_registry.GetThreadLocked(tid));
}

static bool IsInStackOrTls(ThreadContextBase *tctx_base, void *arg) {
  uptr addr = (uptr)arg;
  ThreadContext *tctx = static_cast<ThreadContext*>(tctx_base);
  if (tctx->status != ThreadStatusRunning)
    return false;
  ThreadState *thr = tctx->thr;
  CHECK(thr);
  return ((addr >= thr->stk_addr && addr < thr->stk_addr + thr->stk_size) ||
          (addr >= thr->tls_addr && addr < thr->tls_addr + thr->tls_size));
}

ThreadContext *IsThreadStackOrTls(uptr addr, bool *is_stack) {
  ctx->thread_registry.CheckLocked();
  ThreadContext *tctx =
      static_cast<ThreadContext *>(ctx->thread_registry.FindThreadContextLocked(
          IsInStackOrTls, (void *)addr));
  if (!tctx)
    return 0;
  ThreadState *thr = tctx->thr;
  CHECK(thr);
  *is_stack = (addr >= thr->stk_addr && addr < thr->stk_addr + thr->stk_size);
  return tctx;
}
#endif

void ScopedReportBase::AddThread(Tid tid, bool suppressable) {
#if !SANITIZER_GO
  if (const ThreadContext *tctx = FindThreadByTidLocked(tid))
    AddThread(tctx, suppressable);
#endif
}

int ScopedReportBase::AddMutex(uptr addr, StackID creation_stack_id) {
  for (uptr i = 0; i < rep_->mutexes.Size(); i++) {
    if (rep_->mutexes[i]->addr == addr)
      return rep_->mutexes[i]->id;
  }
  auto *rm = New<ReportMutex>();
  rep_->mutexes.PushBack(rm);
  rm->id = rep_->mutexes.Size() - 1;
  rm->addr = addr;
  rm->stack = SymbolizeStackId(creation_stack_id);
  return rm->id;
}

void ScopedReportBase::AddLocation(uptr addr, uptr size) {
  if (addr == 0)
    return;
#if !SANITIZER_GO
  int fd = -1;
  Tid creat_tid = kInvalidTid;
  StackID creat_stack = 0;
  bool closed = false;
  if (FdLocation(addr, &fd, &creat_tid, &creat_stack, &closed)) {
    auto *loc = New<ReportLocation>();
    loc->type = ReportLocationFD;
    loc->fd_closed = closed;
    loc->fd = fd;
    loc->tid = creat_tid;
    loc->stack = SymbolizeStackId(creat_stack);
    rep_->locs.PushBack(loc);
    AddThread(creat_tid);
    return;
  }
  MBlock *b = 0;
  uptr block_begin = 0;
  Allocator *a = allocator();
  if (a->PointerIsMine((void*)addr)) {
    block_begin = (uptr)a->GetBlockBegin((void *)addr);
    if (block_begin)
      b = ctx->metamap.GetBlock(block_begin);
  }
  if (!b)
    b = JavaHeapBlock(addr, &block_begin);
  if (b != 0) {
    auto *loc = New<ReportLocation>();
    loc->type = ReportLocationHeap;
    loc->heap_chunk_start = block_begin;
    loc->heap_chunk_size = b->siz;
    loc->external_tag = b->tag;
    loc->tid = b->tid;
    loc->stack = SymbolizeStackId(b->stk);
    rep_->locs.PushBack(loc);
    AddThread(b->tid);
    return;
  }
  bool is_stack = false;
  if (ThreadContext *tctx = IsThreadStackOrTls(addr, &is_stack)) {
    auto *loc = New<ReportLocation>();
    loc->type = is_stack ? ReportLocationStack : ReportLocationTLS;
    loc->tid = tctx->tid;
    rep_->locs.PushBack(loc);
    AddThread(tctx);
  }
#endif
  if (ReportLocation *loc = SymbolizeData(addr)) {
    loc->suppressable = true;
    rep_->locs.PushBack(loc);
    return;
  }
}

#if !SANITIZER_GO
void ScopedReportBase::AddSleep(StackID stack_id) {
  rep_->sleep = SymbolizeStackId(stack_id);
}
#endif

void ScopedReportBase::SetCount(int count) { rep_->count = count; }

void ScopedReportBase::SetSigNum(int sig) { rep_->signum = sig; }

const ReportDesc *ScopedReportBase::GetReport() const { return rep_; }

ScopedReport::ScopedReport(ReportType typ, uptr tag)
    : ScopedReportBase(typ, tag) {}

ScopedReport::~ScopedReport() {}

// Replays the trace up to last_pos position in the last part
// or up to the provided epoch/sid (whichever is earlier)
// and calls the provided function f for each event.
template <typename Func>
void TraceReplay(Trace *trace, TracePart *last, Event *last_pos, Sid sid,
                 Epoch epoch, Func f) {
  TracePart *part = trace->parts.Front();
  Sid ev_sid = kFreeSid;
  Epoch ev_epoch = kEpochOver;
  for (;;) {
    DCHECK_EQ(part->trace, trace);
    // Note: an event can't start in the last element.
    // Since an event can take up to 2 elements,
    // we ensure we have at least 2 before adding an event.
    Event *end = &part->events[TracePart::kSize - 1];
    if (part == last)
      end = last_pos;
    f(kFreeSid, kEpochOver, nullptr);  // notify about part start
    for (Event *evp = &part->events[0]; evp < end; evp++) {
      Event *evp0 = evp;
      if (!evp->is_access && !evp->is_func) {
        switch (evp->type) {
          case EventType::kTime: {
            auto *ev = reinterpret_cast<EventTime *>(evp);
            ev_sid = static_cast<Sid>(ev->sid);
            ev_epoch = static_cast<Epoch>(ev->epoch);
            if (ev_sid == sid && ev_epoch > epoch)
              return;
            break;
          }
          case EventType::kAccessExt:
            FALLTHROUGH;
          case EventType::kAccessRange:
            FALLTHROUGH;
          case EventType::kLock:
            FALLTHROUGH;
          case EventType::kRLock:
            // These take 2 Event elements.
            evp++;
            break;
          case EventType::kUnlock:
            // This takes 1 Event element.
            break;
        }
      }
      CHECK_NE(ev_sid, kFreeSid);
      CHECK_NE(ev_epoch, kEpochOver);
      f(ev_sid, ev_epoch, evp0);
    }
    if (part == last)
      return;
    part = trace->parts.Next(part);
    CHECK(part);
  }
  CHECK(0);
}

static void RestoreStackMatch(VarSizeStackTrace *pstk, MutexSet *pmset,
                              Vector<uptr> *stack, MutexSet *mset, uptr pc,
                              bool *found) {
  DPrintf2("    MATCHED\n");
  *pmset = *mset;
  stack->PushBack(pc);
  pstk->Init(&(*stack)[0], stack->Size());
  stack->PopBack();
  *found = true;
}

// Checks if addr1|size1 is fully contained in addr2|size2.
// We check for fully contained instread of just overlapping
// because a memory access is always traced once, but can be
// split into multiple accesses in the shadow.
static constexpr bool IsWithinAccess(uptr addr1, uptr size1, uptr addr2,
                                     uptr size2) {
  return addr1 >= addr2 && addr1 + size1 <= addr2 + size2;
}

// Replays the trace of slot sid up to the target event identified
// by epoch/addr/size/typ and restores and returns tid, stack, mutex set
// and tag for that event. If there are multiple such events, it returns
// the last one. Returns false if the event is not present in the trace.
bool RestoreStack(EventType type, Sid sid, Epoch epoch, uptr addr, uptr size,
                  AccessType typ, Tid *ptid, VarSizeStackTrace *pstk,
                  MutexSet *pmset, uptr *ptag) {
  // This function restores stack trace and mutex set for the thread/epoch.
  // It does so by getting stack trace and mutex set at the beginning of
  // trace part, and then replaying the trace till the given epoch.
  DPrintf2("RestoreStack: sid=%u@%u addr=0x%zx/%zu typ=%x\n",
           static_cast<int>(sid), static_cast<int>(epoch), addr, size,
           static_cast<int>(typ));
  ctx->slot_mtx.CheckLocked();  // needed to prevent trace part recycling
  ctx->thread_registry.CheckLocked();
  TidSlot *slot = &ctx->slots[static_cast<uptr>(sid)];
  Tid tid = kInvalidTid;
  // Need to lock the slot mutex as it protects slot->journal.
  slot->mtx.CheckLocked();
  for (uptr i = 0; i < slot->journal.Size(); i++) {
    DPrintf2("  journal: epoch=%d tid=%d\n",
             static_cast<int>(slot->journal[i].epoch), slot->journal[i].tid);
    if (i == slot->journal.Size() - 1 || slot->journal[i + 1].epoch > epoch) {
      tid = slot->journal[i].tid;
      break;
    }
  }
  if (tid == kInvalidTid)
    return false;
  *ptid = tid;
  ThreadContext *tctx =
      static_cast<ThreadContext *>(ctx->thread_registry.GetThreadLocked(tid));
  Trace *trace = &tctx->trace;
  // Snapshot first/last parts and the current position in the last part.
  TracePart *first_part;
  TracePart *last_part;
  Event *last_pos;
  {
    Lock lock(&trace->mtx);
    first_part = trace->parts.Front();
    if (!first_part) {
      DPrintf2("RestoreStack: tid=%d trace=%p no trace parts\n", tid, trace);
      return false;
    }
    last_part = trace->parts.Back();
    last_pos = trace->final_pos;
    if (tctx->thr)
      last_pos = (Event *)atomic_load_relaxed(&tctx->thr->trace_pos);
  }
  DynamicMutexSet mset;
  Vector<uptr> stack;
  uptr prev_pc = 0;
  bool found = false;
  bool is_read = typ & kAccessRead;
  bool is_atomic = typ & kAccessAtomic;
  bool is_free = typ & kAccessFree;
  DPrintf2("RestoreStack: tid=%d parts=[%p-%p] last_pos=%p\n", tid,
           trace->parts.Front(), last_part, last_pos);
  TraceReplay(
      trace, last_part, last_pos, sid, epoch,
      [&](Sid ev_sid, Epoch ev_epoch, Event *evp) {
        if (evp == nullptr) {
          // Each trace part is self-consistent, so we reset state.
          stack.Resize(0);
          mset->Reset();
          prev_pc = 0;
          return;
        }
        bool match = ev_sid == sid && ev_epoch == epoch;
        if (evp->is_access) {
          if (evp->is_func == 0 && evp->type == EventType::kAccessExt &&
              evp->_ == 0)  // NopEvent
            return;
          auto *ev = reinterpret_cast<EventAccess *>(evp);
          uptr ev_addr = RestoreAddr(ev->addr);
          uptr ev_size = 1 << ev->size_log;
          uptr ev_pc =
              prev_pc + ev->pc_delta - (1 << (EventAccess::kPCBits - 1));
          prev_pc = ev_pc;
          DPrintf2("  Access: pc=0x%zx addr=0x%zx/%zu type=%u/%u\n", ev_pc,
                   ev_addr, ev_size, ev->is_read, ev->is_atomic);
          if (match && type == EventType::kAccessExt &&
              IsWithinAccess(addr, size, ev_addr, ev_size) &&
              is_read == ev->is_read && is_atomic == ev->is_atomic && !is_free)
            RestoreStackMatch(pstk, pmset, &stack, mset, ev_pc, &found);
          return;
        }
        if (evp->is_func) {
          auto *ev = reinterpret_cast<EventFunc *>(evp);
          if (ev->pc) {
            DPrintf2(" FuncEnter: pc=0x%llx\n", ev->pc);
            stack.PushBack(ev->pc);
          } else {
            DPrintf2(" FuncExit\n");
            // We don't log pathologically large stacks in each part,
            // if the stack was truncated we can have more func exits than
            // entries.
            if (stack.Size())
              stack.PopBack();
          }
          return;
        }
        switch (evp->type) {
          case EventType::kAccessExt: {
            auto *ev = reinterpret_cast<EventAccessExt *>(evp);
            uptr ev_addr = RestoreAddr(ev->addr);
            uptr ev_size = 1 << ev->size_log;
            prev_pc = ev->pc;
            DPrintf2("  AccessExt: pc=0x%llx addr=0x%zx/%zu type=%u/%u\n",
                     ev->pc, ev_addr, ev_size, ev->is_read, ev->is_atomic);
            if (match && type == EventType::kAccessExt &&
                IsWithinAccess(addr, size, ev_addr, ev_size) &&
                is_read == ev->is_read && is_atomic == ev->is_atomic &&
                !is_free)
              RestoreStackMatch(pstk, pmset, &stack, mset, ev->pc, &found);
            break;
          }
          case EventType::kAccessRange: {
            auto *ev = reinterpret_cast<EventAccessRange *>(evp);
            uptr ev_addr = RestoreAddr(ev->addr);
            uptr ev_size =
                (ev->size_hi << EventAccessRange::kSizeLoBits) + ev->size_lo;
            uptr ev_pc = RestoreAddr(ev->pc);
            prev_pc = ev_pc;
            DPrintf2("  Range: pc=0x%zx addr=0x%zx/%zu type=%u/%u\n", ev_pc,
                     ev_addr, ev_size, ev->is_read, ev->is_free);
            if (match && type == EventType::kAccessExt &&
                IsWithinAccess(addr, size, ev_addr, ev_size) &&
                is_read == ev->is_read && !is_atomic && is_free == ev->is_free)
              RestoreStackMatch(pstk, pmset, &stack, mset, ev_pc, &found);
            break;
          }
          case EventType::kLock:
            FALLTHROUGH;
          case EventType::kRLock: {
            auto *ev = reinterpret_cast<EventLock *>(evp);
            bool is_write = ev->type == EventType::kLock;
            uptr ev_addr = RestoreAddr(ev->addr);
            uptr ev_pc = RestoreAddr(ev->pc);
            StackID stack_id =
                (ev->stack_hi << EventLock::kStackIDLoBits) + ev->stack_lo;
            DPrintf2("  Lock: pc=0x%zx addr=0x%zx stack=%u write=%d\n", ev_pc,
                     ev_addr, stack_id, is_write);
            mset->AddAddr(ev_addr, stack_id, is_write);
            // Events with ev_pc == 0 are written to the beginning of trace
            // part as initial mutex set (are not real).
            if (match && type == EventType::kLock && addr == ev_addr && ev_pc)
              RestoreStackMatch(pstk, pmset, &stack, mset, ev_pc, &found);
            break;
          }
          case EventType::kUnlock: {
            auto *ev = reinterpret_cast<EventUnlock *>(evp);
            uptr ev_addr = RestoreAddr(ev->addr);
            DPrintf2("  Unlock: addr=0x%zx\n", ev_addr);
            mset->DelAddr(ev_addr);
            break;
          }
          case EventType::kTime:
            // TraceReplay already extracted sid/epoch from it,
            // nothing else to do here.
            break;
        }
      });
  ExtractTagFromStack(pstk, ptag);
  return found;
}

bool RacyStacks::operator==(const RacyStacks &other) const {
  if (hash[0] == other.hash[0] && hash[1] == other.hash[1])
    return true;
  if (hash[0] == other.hash[1] && hash[1] == other.hash[0])
    return true;
  return false;
}

static bool FindRacyStacks(const RacyStacks &hash) {
  for (uptr i = 0; i < ctx->racy_stacks.Size(); i++) {
    if (hash == ctx->racy_stacks[i]) {
      VPrintf(2, "ThreadSanitizer: suppressing report as doubled (stack)\n");
      return true;
    }
  }
  return false;
}

static bool HandleRacyStacks(ThreadState *thr, VarSizeStackTrace traces[2]) {
  if (!flags()->suppress_equal_stacks)
    return false;
  RacyStacks hash;
  hash.hash[0] = md5_hash(traces[0].trace, traces[0].size * sizeof(uptr));
  hash.hash[1] = md5_hash(traces[1].trace, traces[1].size * sizeof(uptr));
  {
    ReadLock lock(&ctx->racy_mtx);
    if (FindRacyStacks(hash))
      return true;
  }
  Lock lock(&ctx->racy_mtx);
  if (FindRacyStacks(hash))
    return true;
  ctx->racy_stacks.PushBack(hash);
  return false;
}

bool OutputReport(ThreadState *thr, const ScopedReport &srep) {
  // These should have been checked in ShouldReport.
  // It's too late to check them here, we have already taken locks.
  CHECK(flags()->report_bugs);
  CHECK(!thr->suppress_reports);
  atomic_store_relaxed(&ctx->last_symbolize_time_ns, NanoTime());
  const ReportDesc *rep = srep.GetReport();
  CHECK_EQ(thr->current_report, nullptr);
  thr->current_report = rep;
  Suppression *supp = 0;
  uptr pc_or_addr = 0;
  for (uptr i = 0; pc_or_addr == 0 && i < rep->mops.Size(); i++)
    pc_or_addr = IsSuppressed(rep->typ, rep->mops[i]->stack, &supp);
  for (uptr i = 0; pc_or_addr == 0 && i < rep->stacks.Size(); i++)
    pc_or_addr = IsSuppressed(rep->typ, rep->stacks[i], &supp);
  for (uptr i = 0; pc_or_addr == 0 && i < rep->threads.Size(); i++)
    pc_or_addr = IsSuppressed(rep->typ, rep->threads[i]->stack, &supp);
  for (uptr i = 0; pc_or_addr == 0 && i < rep->locs.Size(); i++)
    pc_or_addr = IsSuppressed(rep->typ, rep->locs[i], &supp);
  if (pc_or_addr != 0) {
    Lock lock(&ctx->fired_suppressions_mtx);
    FiredSuppression s = {srep.GetReport()->typ, pc_or_addr, supp};
    ctx->fired_suppressions.push_back(s);
  }
  {
    bool suppressed = OnReport(rep, pc_or_addr != 0);
    if (suppressed) {
      thr->current_report = nullptr;
      return false;
    }
  }
  PrintReport(rep);
  __tsan_on_report(rep);
  ctx->nreported++;
  if (flags()->halt_on_error)
    Die();
  thr->current_report = nullptr;
  return true;
}

bool IsFiredSuppression(Context *ctx, ReportType type, StackTrace trace) {
  ReadLock lock(&ctx->fired_suppressions_mtx);
  for (uptr k = 0; k < ctx->fired_suppressions.size(); k++) {
    if (ctx->fired_suppressions[k].type != type)
      continue;
    for (uptr j = 0; j < trace.size; j++) {
      FiredSuppression *s = &ctx->fired_suppressions[k];
      if (trace.trace[j] == s->pc_or_addr) {
        if (s->supp)
          atomic_fetch_add(&s->supp->hit_count, 1, memory_order_relaxed);
        return true;
      }
    }
  }
  return false;
}

static bool IsFiredSuppression(Context *ctx, ReportType type, uptr addr) {
  ReadLock lock(&ctx->fired_suppressions_mtx);
  for (uptr k = 0; k < ctx->fired_suppressions.size(); k++) {
    if (ctx->fired_suppressions[k].type != type)
      continue;
    FiredSuppression *s = &ctx->fired_suppressions[k];
    if (addr == s->pc_or_addr) {
      if (s->supp)
        atomic_fetch_add(&s->supp->hit_count, 1, memory_order_relaxed);
      return true;
    }
  }
  return false;
}

static bool SpuriousRace(Shadow old) {
  Shadow last(LoadShadow(&ctx->last_spurious_race));
  return last.sid() == old.sid() && last.epoch() == old.epoch();
}

void ReportRace(ThreadState *thr, RawShadow *shadow_mem, Shadow cur, Shadow old,
                AccessType typ0) {
  CheckedMutex::CheckNoLocks();

  // Symbolizer makes lots of intercepted calls. If we try to process them,
  // at best it will cause deadlocks on internal mutexes.
  ScopedIgnoreInterceptors ignore;

  uptr addr = ShadowToMem(shadow_mem);
  DPrintf("#%d: ReportRace %p\n", thr->tid, (void *)addr);
  if (!ShouldReport(thr, ReportTypeRace))
    return;
  uptr addr_off0, size0;
  cur.GetAccess(&addr_off0, &size0, nullptr);
  uptr addr_off1, size1, typ1;
  old.GetAccess(&addr_off1, &size1, &typ1);
  if (!flags()->report_atomic_races &&
      ((typ0 & kAccessAtomic) || (typ1 & kAccessAtomic)) &&
      !(typ0 & kAccessFree) && !(typ1 & kAccessFree))
    return;
  if (SpuriousRace(old))
    return;

  const uptr kMop = 2;
  Shadow s[kMop] = {cur, old};
  uptr addr0 = addr + addr_off0;
  uptr addr1 = addr + addr_off1;
  uptr end0 = addr0 + size0;
  uptr end1 = addr1 + size1;
  uptr addr_min = min(addr0, addr1);
  uptr addr_max = max(end0, end1);
  if (IsExpectedReport(addr_min, addr_max - addr_min))
    return;

  ReportType rep_typ = ReportTypeRace;
  if ((typ0 & kAccessVptr) && (typ1 & kAccessFree))
    rep_typ = ReportTypeVptrUseAfterFree;
  else if (typ0 & kAccessVptr)
    rep_typ = ReportTypeVptrRace;
  else if (typ1 & kAccessFree)
    rep_typ = ReportTypeUseAfterFree;

  if (IsFiredSuppression(ctx, rep_typ, addr))
    return;

  VarSizeStackTrace traces[kMop];
  Tid tids[kMop] = {thr->tid, kInvalidTid};
  uptr tags[kMop] = {kExternalTagNone, kExternalTagNone};

  ObtainCurrentStack(thr, thr->trace_prev_pc, &traces[0], &tags[0]);
  if (IsFiredSuppression(ctx, rep_typ, traces[0]))
    return;

  DynamicMutexSet mset1;
  MutexSet *mset[kMop] = {&thr->mset, mset1};

  // We need to lock the slot during RestoreStack because it protects
  // the slot journal.
  Lock slot_lock(&ctx->slots[static_cast<uptr>(s[1].sid())].mtx);
  ThreadRegistryLock l0(&ctx->thread_registry);
  Lock slots_lock(&ctx->slot_mtx);
  if (SpuriousRace(old))
    return;
  if (!RestoreStack(EventType::kAccessExt, s[1].sid(), s[1].epoch(), addr1,
                    size1, typ1, &tids[1], &traces[1], mset[1], &tags[1])) {
    StoreShadow(&ctx->last_spurious_race, old.raw());
    return;
  }

  if (IsFiredSuppression(ctx, rep_typ, traces[1]))
    return;

  if (HandleRacyStacks(thr, traces))
    return;

  // If any of the accesses has a tag, treat this as an "external" race.
  uptr tag = kExternalTagNone;
  for (uptr i = 0; i < kMop; i++) {
    if (tags[i] != kExternalTagNone) {
      rep_typ = ReportTypeExternalRace;
      tag = tags[i];
      break;
    }
  }

  ScopedReport rep(rep_typ, tag);
  for (uptr i = 0; i < kMop; i++)
    rep.AddMemoryAccess(addr, tags[i], s[i], tids[i], traces[i], mset[i]);

  for (uptr i = 0; i < kMop; i++) {
    ThreadContext *tctx = static_cast<ThreadContext *>(
        ctx->thread_registry.GetThreadLocked(tids[i]));
    rep.AddThread(tctx);
  }

  rep.AddLocation(addr_min, addr_max - addr_min);

  if (flags()->print_full_thread_history) {
    const ReportDesc *rep_desc = rep.GetReport();
    for (uptr i = 0; i < rep_desc->threads.Size(); i++) {
      Tid parent_tid = rep_desc->threads[i]->parent_tid;
      if (parent_tid == kMainTid || parent_tid == kInvalidTid)
        continue;
      ThreadContext *parent_tctx = static_cast<ThreadContext *>(
          ctx->thread_registry.GetThreadLocked(parent_tid));
      rep.AddThread(parent_tctx);
    }
  }

#if !SANITIZER_GO
  if (!((typ0 | typ1) & kAccessFree) &&
      s[1].epoch() <= thr->last_sleep_clock.Get(s[1].sid()))
    rep.AddSleep(thr->last_sleep_stack_id);
#endif
  OutputReport(thr, rep);
}

void PrintCurrentStack(ThreadState *thr, uptr pc) {
  VarSizeStackTrace trace;
  ObtainCurrentStack(thr, pc, &trace);
  PrintStack(SymbolizeStack(trace));
}

// Always inlining PrintCurrentStackSlow, because LocatePcInTrace assumes
// __sanitizer_print_stack_trace exists in the actual unwinded stack, but
// tail-call to PrintCurrentStackSlow breaks this assumption because
// __sanitizer_print_stack_trace disappears after tail-call.
// However, this solution is not reliable enough, please see dvyukov's comment
// http://reviews.llvm.org/D19148#406208
// Also see PR27280 comment 2 and 3 for breaking examples and analysis.
ALWAYS_INLINE USED void PrintCurrentStackSlow(uptr pc) {
#if !SANITIZER_GO
  uptr bp = GET_CURRENT_FRAME();
  auto *ptrace = New<BufferedStackTrace>();
  ptrace->Unwind(pc, bp, nullptr, false);

  for (uptr i = 0; i < ptrace->size / 2; i++) {
    uptr tmp = ptrace->trace_buffer[i];
    ptrace->trace_buffer[i] = ptrace->trace_buffer[ptrace->size - i - 1];
    ptrace->trace_buffer[ptrace->size - i - 1] = tmp;
  }
  PrintStack(SymbolizeStack(*ptrace));
#endif
}

}  // namespace __tsan

using namespace __tsan;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  PrintCurrentStackSlow(StackTrace::GetCurrentPc());
}
}  // extern "C"
