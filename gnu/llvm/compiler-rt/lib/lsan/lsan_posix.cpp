//=-- lsan_posix.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code common to POSIX-like systems.
//
//===---------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

#if SANITIZER_POSIX
#  include <pthread.h>

#  include "lsan.h"
#  include "lsan_allocator.h"
#  include "lsan_thread.h"
#  include "sanitizer_common/sanitizer_stacktrace.h"
#  include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __lsan {

ThreadContext::ThreadContext(int tid) : ThreadContextLsanBase(tid) {}

struct OnStartedArgs {
  uptr stack_begin;
  uptr stack_end;
  uptr cache_begin;
  uptr cache_end;
  uptr tls_begin;
  uptr tls_end;
  DTLS *dtls;
};

void ThreadContext::OnStarted(void *arg) {
  ThreadContextLsanBase::OnStarted(arg);
  auto args = reinterpret_cast<const OnStartedArgs *>(arg);
  stack_begin_ = args->stack_begin;
  stack_end_ = args->stack_end;
  tls_begin_ = args->tls_begin;
  tls_end_ = args->tls_end;
  cache_begin_ = args->cache_begin;
  cache_end_ = args->cache_end;
  dtls_ = args->dtls;
}

void ThreadStart(u32 tid, tid_t os_id, ThreadType thread_type) {
  OnStartedArgs args;
  uptr stack_size = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(tid == kMainTid, &args.stack_begin, &stack_size,
                       &args.tls_begin, &tls_size);
  args.stack_end = args.stack_begin + stack_size;
  args.tls_end = args.tls_begin + tls_size;
  GetAllocatorCacheRange(&args.cache_begin, &args.cache_end);
  args.dtls = DTLS_Get();
  ThreadContextLsanBase::ThreadStart(tid, os_id, thread_type, &args);
}

bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  ThreadContext *context = static_cast<ThreadContext *>(
      GetLsanThreadRegistryLocked()->FindThreadContextByOsIDLocked(os_id));
  if (!context)
    return false;
  *stack_begin = context->stack_begin();
  *stack_end = context->stack_end();
  *tls_begin = context->tls_begin();
  *tls_end = context->tls_end();
  *cache_begin = context->cache_begin();
  *cache_end = context->cache_end();
  *dtls = context->dtls();
  return true;
}

void InitializeMainThread() {
  u32 tid = ThreadCreate(kMainTid, true);
  CHECK_EQ(tid, kMainTid);
  ThreadStart(tid, GetTid());
}

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  stack->Unwind(StackTrace::GetNextInstructionPc(sig.pc), sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

void LsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetCurrentThreadId(), &OnStackUnwind,
                     nullptr);
}

void InstallAtExitCheckLeaks() {
  if (common_flags()->detect_leaks && common_flags()->leak_check_at_exit)
    Atexit(DoLeakCheck);
}

static void BeforeFork() {
  LockGlobal();
  LockThreads();
  LockAllocator();
  StackDepotLockBeforeFork();
}

static void AfterFork(bool fork_child) {
  StackDepotUnlockAfterFork(fork_child);
  UnlockAllocator();
  UnlockThreads();
  UnlockGlobal();
}

void InstallAtForkHandler() {
#  if SANITIZER_SOLARIS || SANITIZER_NETBSD || SANITIZER_APPLE
  return;  // FIXME: Implement FutexWait.
#  endif
  pthread_atfork(
      &BeforeFork, []() { AfterFork(/* fork_child= */ false); },
      []() { AfterFork(/* fork_child= */ true); });
}

}  // namespace __lsan

#endif  // SANITIZER_POSIX
