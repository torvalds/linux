//===-- memprof_thread.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Thread-related code.
//===----------------------------------------------------------------------===//
#include "memprof_thread.h"
#include "memprof_allocator.h"
#include "memprof_interceptors.h"
#include "memprof_mapping.h"
#include "memprof_stack.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __memprof {

// MemprofThreadContext implementation.

void MemprofThreadContext::OnCreated(void *arg) {
  CreateThreadContextArgs *args = static_cast<CreateThreadContextArgs *>(arg);
  if (args->stack)
    stack_id = StackDepotPut(*args->stack);
  thread = args->thread;
  thread->set_context(this);
}

void MemprofThreadContext::OnFinished() {
  // Drop the link to the MemprofThread object.
  thread = nullptr;
}

alignas(16) static char thread_registry_placeholder[sizeof(ThreadRegistry)];
static ThreadRegistry *memprof_thread_registry;

static Mutex mu_for_thread_context;
static LowLevelAllocator allocator_for_thread_context;

static ThreadContextBase *GetMemprofThreadContext(u32 tid) {
  Lock lock(&mu_for_thread_context);
  return new (allocator_for_thread_context) MemprofThreadContext(tid);
}

ThreadRegistry &memprofThreadRegistry() {
  static bool initialized;
  // Don't worry about thread_safety - this should be called when there is
  // a single thread.
  if (!initialized) {
    // Never reuse MemProf threads: we store pointer to MemprofThreadContext
    // in TSD and can't reliably tell when no more TSD destructors will
    // be called. It would be wrong to reuse MemprofThreadContext for another
    // thread before all TSD destructors will be called for it.
    memprof_thread_registry = new (thread_registry_placeholder)
        ThreadRegistry(GetMemprofThreadContext);
    initialized = true;
  }
  return *memprof_thread_registry;
}

MemprofThreadContext *GetThreadContextByTidLocked(u32 tid) {
  return static_cast<MemprofThreadContext *>(
      memprofThreadRegistry().GetThreadLocked(tid));
}

// MemprofThread implementation.

MemprofThread *MemprofThread::Create(thread_callback_t start_routine, void *arg,
                                     u32 parent_tid, StackTrace *stack,
                                     bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(MemprofThread), PageSize);
  MemprofThread *thread = (MemprofThread *)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  MemprofThreadContext::CreateThreadContextArgs args = {thread, stack};
  memprofThreadRegistry().CreateThread(0, detached, parent_tid, &args);

  return thread;
}

void MemprofThread::TSDDtor(void *tsd) {
  MemprofThreadContext *context = (MemprofThreadContext *)tsd;
  VReport(1, "T%d TSDDtor\n", context->tid);
  if (context->thread)
    context->thread->Destroy();
}

void MemprofThread::Destroy() {
  int tid = this->tid();
  VReport(1, "T%d exited\n", tid);

  malloc_storage().CommitBack();
  memprofThreadRegistry().FinishThread(tid);
  FlushToDeadThreadStats(&stats_);
  uptr size = RoundUpTo(sizeof(MemprofThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

inline MemprofThread::StackBounds MemprofThread::GetStackBounds() const {
  if (stack_bottom_ >= stack_top_)
    return {0, 0};
  return {stack_bottom_, stack_top_};
}

uptr MemprofThread::stack_top() { return GetStackBounds().top; }

uptr MemprofThread::stack_bottom() { return GetStackBounds().bottom; }

uptr MemprofThread::stack_size() {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}

void MemprofThread::Init(const InitOptions *options) {
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  if (stack_top_ != stack_bottom_) {
    CHECK_GT(this->stack_size(), 0U);
    CHECK(AddrIsInMem(stack_bottom_));
    CHECK(AddrIsInMem(stack_top_ - 1));
  }
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          (void *)&local);
}

thread_return_t
MemprofThread::ThreadStart(tid_t os_id,
                           atomic_uintptr_t *signal_thread_is_registered) {
  Init();
  memprofThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular,
                                      nullptr);
  if (signal_thread_is_registered)
    atomic_store(signal_thread_is_registered, 1, memory_order_release);

  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    CHECK_EQ(tid(), 0);
    return 0;
  }

  return start_routine_(arg_);
}

MemprofThread *CreateMainThread() {
  MemprofThread *main_thread = MemprofThread::Create(
      /* start_routine */ nullptr, /* arg */ nullptr, /* parent_tid */ kMainTid,
      /* stack */ nullptr, /* detached */ true);
  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid(),
                           /* signal_thread_is_registered */ nullptr);
  return main_thread;
}

// This implementation doesn't use the argument, which is just passed down
// from the caller of Init (which see, above).  It's only there to support
// OS-specific implementations that need more information passed through.
void MemprofThread::SetThreadStackAndTls(const InitOptions *options) {
  DCHECK_EQ(options, nullptr);
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(tid() == kMainTid, &stack_bottom_, &stack_size,
                       &tls_begin_, &tls_size);
  stack_top_ = stack_bottom_ + stack_size;
  tls_end_ = tls_begin_ + tls_size;
  dtls_ = DTLS_Get();

  if (stack_top_ != stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
  }
}

bool MemprofThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

MemprofThread *GetCurrentThread() {
  MemprofThreadContext *context =
      reinterpret_cast<MemprofThreadContext *>(TSDGet());
  if (!context)
    return nullptr;
  return context->thread;
}

void SetCurrentThread(MemprofThread *t) {
  CHECK(t->context());
  VReport(2, "SetCurrentThread: %p for thread %p\n", (void *)t->context(),
          (void *)GetThreadSelf());
  // Make sure we do not reset the current MemprofThread.
  CHECK_EQ(0, TSDGet());
  TSDSet(t->context());
  CHECK_EQ(t->context(), TSDGet());
}

u32 GetCurrentTidOrInvalid() {
  MemprofThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

void EnsureMainThreadIDIsCorrect() {
  MemprofThreadContext *context =
      reinterpret_cast<MemprofThreadContext *>(TSDGet());
  if (context && (context->tid == kMainTid))
    context->os_id = GetTid();
}
} // namespace __memprof
