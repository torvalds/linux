//=-- lsan_fuchsia.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code specific to Fuchsia.
//
//===---------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

#if SANITIZER_FUCHSIA
#include <zircon/sanitizer.h>

#include "lsan.h"
#include "lsan_allocator.h"

using namespace __lsan;

namespace __lsan {

void LsanOnDeadlySignal(int signo, void *siginfo, void *context) {}

ThreadContext::ThreadContext(int tid) : ThreadContextLsanBase(tid) {}

struct OnCreatedArgs {
  uptr stack_begin, stack_end;
};

// On Fuchsia, the stack bounds of a new thread are available before
// the thread itself has started running.
void ThreadContext::OnCreated(void *arg) {
  // Stack bounds passed through from __sanitizer_before_thread_create_hook
  // or InitializeMainThread.
  auto args = reinterpret_cast<const OnCreatedArgs *>(arg);
  stack_begin_ = args->stack_begin;
  stack_end_ = args->stack_end;
}

struct OnStartedArgs {
  uptr cache_begin, cache_end;
};

void ThreadContext::OnStarted(void *arg) {
  ThreadContextLsanBase::OnStarted(arg);
  auto args = reinterpret_cast<const OnStartedArgs *>(arg);
  cache_begin_ = args->cache_begin;
  cache_end_ = args->cache_end;
}

void ThreadStart(u32 tid) {
  OnStartedArgs args;
  GetAllocatorCacheRange(&args.cache_begin, &args.cache_end);
  CHECK_EQ(args.cache_end - args.cache_begin, sizeof(AllocatorCache));
  ThreadContextLsanBase::ThreadStart(tid, GetTid(), ThreadType::Regular, &args);
}

void InitializeMainThread() {
  OnCreatedArgs args;
  __sanitizer::GetThreadStackTopAndBottom(true, &args.stack_end,
                                          &args.stack_begin);
  u32 tid = ThreadCreate(kMainTid, true, &args);
  CHECK_EQ(tid, 0);
  ThreadStart(tid);
}

void GetAllThreadAllocatorCachesLocked(InternalMmapVector<uptr> *caches) {
  GetLsanThreadRegistryLocked()->RunCallbackForEachThreadLocked(
      [](ThreadContextBase *tctx, void *arg) {
        auto ctx = static_cast<ThreadContext *>(tctx);
        static_cast<decltype(caches)>(arg)->push_back(ctx->cache_begin());
      },
      caches);
}

// On Fuchsia, leak detection is done by a special hook after atexit hooks.
// So this doesn't install any atexit hook like on other platforms.
void InstallAtExitCheckLeaks() {}
void InstallAtForkHandler() {}

// ASan defines this to check its `halt_on_error` flag.
bool UseExitcodeOnLeak() { return true; }

}  // namespace __lsan

// These are declared (in extern "C") by <zircon/sanitizer.h>.
// The system runtime will call our definitions directly.

// This is called before each thread creation is attempted.  So, in
// its first call, the calling thread is the initial and sole thread.
void *__sanitizer_before_thread_create_hook(thrd_t thread, bool detached,
                                            const char *name, void *stack_base,
                                            size_t stack_size) {
  ENSURE_LSAN_INITED;
  EnsureMainThreadIDIsCorrect();
  OnCreatedArgs args;
  args.stack_begin = reinterpret_cast<uptr>(stack_base);
  args.stack_end = args.stack_begin + stack_size;
  u32 parent_tid = GetCurrentThreadId();
  u32 tid = ThreadCreate(parent_tid, detached, &args);
  return reinterpret_cast<void *>(static_cast<uptr>(tid));
}

// This is called after creating a new thread (in the creating thread),
// with the pointer returned by __sanitizer_before_thread_create_hook (above).
void __sanitizer_thread_create_hook(void *hook, thrd_t thread, int error) {
  u32 tid = static_cast<u32>(reinterpret_cast<uptr>(hook));
  // On success, there is nothing to do here.
  if (error != thrd_success) {
    // Clean up the thread registry for the thread creation that didn't happen.
    GetLsanThreadRegistryLocked()->FinishThread(tid);
  }
}

// This is called in the newly-created thread before it runs anything else,
// with the pointer returned by __sanitizer_before_thread_create_hook (above).
void __sanitizer_thread_start_hook(void *hook, thrd_t self) {
  u32 tid = static_cast<u32>(reinterpret_cast<uptr>(hook));
  ThreadStart(tid);
}

// Each thread runs this just before it exits,
// with the pointer returned by BeforeThreadCreateHook (above).
// All per-thread destructors have already been called.
void __sanitizer_thread_exit_hook(void *hook, thrd_t self) { ThreadFinish(); }

#endif  // SANITIZER_FUCHSIA
