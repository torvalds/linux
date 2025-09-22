//===-- asan_posix.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Posix-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_POSIX

#  include <pthread.h>
#  include <signal.h>
#  include <stdlib.h>
#  include <sys/resource.h>
#  include <sys/time.h>
#  include <unistd.h>

#  include "asan_interceptors.h"
#  include "asan_internal.h"
#  include "asan_mapping.h"
#  include "asan_poisoning.h"
#  include "asan_report.h"
#  include "asan_stack.h"
#  include "lsan/lsan_common.h"
#  include "sanitizer_common/sanitizer_libc.h"
#  include "sanitizer_common/sanitizer_posix.h"
#  include "sanitizer_common/sanitizer_procmaps.h"

namespace __asan {

void AsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  StartReportDeadlySignal();
  SignalContext sig(siginfo, context);
  ReportDeadlySignal(sig);
}

bool PlatformUnpoisonStacks() {
  stack_t signal_stack;
  CHECK_EQ(0, sigaltstack(nullptr, &signal_stack));
  uptr sigalt_bottom = (uptr)signal_stack.ss_sp;
  uptr sigalt_top = (uptr)((char *)signal_stack.ss_sp + signal_stack.ss_size);
  // If we're executing on the signal alternate stack AND the Linux flag
  // SS_AUTODISARM was used, then we cannot get the signal alternate stack
  // bounds from sigaltstack -- sigaltstack's output looks just as if no
  // alternate stack has ever been set up.
  // We're always unpoisoning the signal alternate stack to support jumping
  // between the default stack and signal alternate stack.
  if (signal_stack.ss_flags != SS_DISABLE)
    UnpoisonStack(sigalt_bottom, sigalt_top, "sigalt");

  if (signal_stack.ss_flags != SS_ONSTACK)
    return false;

  // Since we're on the signal alternate stack, we cannot find the DEFAULT
  // stack bottom using a local variable.
  uptr default_bottom, tls_addr, tls_size, stack_size;
  GetThreadStackAndTls(/*main=*/false, &default_bottom, &stack_size, &tls_addr,
                       &tls_size);
  UnpoisonStack(default_bottom, default_bottom + stack_size, "default");
  return true;
}

// ---------------------- TSD ---------------- {{{1

#if SANITIZER_NETBSD && !ASAN_DYNAMIC
// Thread Static Data cannot be used in early static ASan init on NetBSD.
// Reuse the Asan TSD API for compatibility with existing code
// with an alternative implementation.

static void (*tsd_destructor)(void *tsd) = nullptr;

struct tsd_key {
  tsd_key() : key(nullptr) {}
  ~tsd_key() {
    CHECK(tsd_destructor);
    if (key)
      (*tsd_destructor)(key);
  }
  void *key;
};

static thread_local struct tsd_key key;

void AsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_destructor);
  tsd_destructor = destructor;
}

void *AsanTSDGet() {
  CHECK(tsd_destructor);
  return key.key;
}

void AsanTSDSet(void *tsd) {
  CHECK(tsd_destructor);
  CHECK(tsd);
  CHECK(!key.key);
  key.key = tsd;
}

void PlatformTSDDtor(void *tsd) {
  CHECK(tsd_destructor);
  CHECK_EQ(key.key, tsd);
  key.key = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  AsanThread::TSDDtor(tsd);
}
#else
static pthread_key_t tsd_key;
static bool tsd_key_inited = false;
void AsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

void *AsanTSDGet() {
  CHECK(tsd_key_inited);
  return pthread_getspecific(tsd_key);
}

void AsanTSDSet(void *tsd) {
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, tsd);
}

void PlatformTSDDtor(void *tsd) {
  AsanThreadContext *context = (AsanThreadContext *)tsd;
  if (context->destructor_iterations > 1) {
    context->destructor_iterations--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  // After this point it's unsafe to execute signal handlers which may be
  // instrumented. It's probably not just a Linux issue.
  BlockSignals();
#    endif
  AsanThread::TSDDtor(tsd);
}
#  endif

static void BeforeFork() {
  if (CAN_SANITIZE_LEAKS) {
    __lsan::LockGlobal();
  }
  // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and lock the
  // stuff we need.
  __lsan::LockThreads();
  __lsan::LockAllocator();
  StackDepotLockBeforeFork();
}

static void AfterFork(bool fork_child) {
  StackDepotUnlockAfterFork(fork_child);
  // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and unlock
  // the stuff we need.
  __lsan::UnlockAllocator();
  __lsan::UnlockThreads();
  if (CAN_SANITIZE_LEAKS) {
    __lsan::UnlockGlobal();
  }
}

void InstallAtForkHandler() {
#  if SANITIZER_SOLARIS || SANITIZER_NETBSD || SANITIZER_APPLE
  return;  // FIXME: Implement FutexWait.
#  endif
  pthread_atfork(
      &BeforeFork, []() { AfterFork(/* fork_child= */ false); },
      []() { AfterFork(/* fork_child= */ true); });
}

void InstallAtExitCheckLeaks() {
  if (CAN_SANITIZE_LEAKS) {
    if (common_flags()->detect_leaks && common_flags()->leak_check_at_exit) {
      if (flags()->halt_on_error)
        Atexit(__lsan::DoLeakCheck);
      else
        Atexit(__lsan::DoRecoverableLeakCheckVoid);
    }
  }
}

}  // namespace __asan

#endif  // SANITIZER_POSIX
