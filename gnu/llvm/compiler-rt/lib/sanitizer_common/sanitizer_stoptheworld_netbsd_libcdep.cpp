//===-- sanitizer_stoptheworld_netbsd_libcdep.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See sanitizer_stoptheworld.h for details.
// This implementation was inspired by Markus Gutschke's linuxthreads.cc.
//
// This is a NetBSD variation of Linux stoptheworld implementation
// See sanitizer_stoptheworld_linux_libcdep.cpp for code comments.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_NETBSD

#include "sanitizer_stoptheworld.h"

#include "sanitizer_atomic.h"
#include "sanitizer_platform_limits_posix.h"

#include <sys/types.h>

#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <machine/reg.h>

#include <elf.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>

#define internal_sigaction_norestorer internal_sigaction

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_linux.h"
#include "sanitizer_mutex.h"
#include "sanitizer_placement_new.h"

namespace __sanitizer {

class SuspendedThreadsListNetBSD final : public SuspendedThreadsList {
 public:
  SuspendedThreadsListNetBSD() { thread_ids_.reserve(1024); }

  tid_t GetThreadID(uptr index) const;
  uptr ThreadCount() const;
  bool ContainsTid(tid_t thread_id) const;
  void Append(tid_t tid);

  PtraceRegistersStatus GetRegistersAndSP(uptr index,
                                          InternalMmapVector<uptr> *buffer,
                                          uptr *sp) const;

 private:
  InternalMmapVector<tid_t> thread_ids_;
};

struct TracerThreadArgument {
  StopTheWorldCallback callback;
  void *callback_argument;
  Mutex mutex;
  atomic_uintptr_t done;
  uptr parent_pid;
};

class ThreadSuspender {
 public:
  explicit ThreadSuspender(pid_t pid, TracerThreadArgument *arg)
      : arg(arg), pid_(pid) {
    CHECK_GE(pid, 0);
  }
  bool SuspendAllThreads();
  void ResumeAllThreads();
  void KillAllThreads();
  SuspendedThreadsListNetBSD &suspended_threads_list() {
    return suspended_threads_list_;
  }
  TracerThreadArgument *arg;

 private:
  SuspendedThreadsListNetBSD suspended_threads_list_;
  pid_t pid_;
};

void ThreadSuspender::ResumeAllThreads() {
  int pterrno;
  if (!internal_iserror(internal_ptrace(PT_DETACH, pid_, (void *)(uptr)1, 0),
                        &pterrno)) {
    VReport(2, "Detached from process %d.\n", pid_);
  } else {
    VReport(1, "Could not detach from process %d (errno %d).\n", pid_, pterrno);
  }
}

void ThreadSuspender::KillAllThreads() {
  internal_ptrace(PT_KILL, pid_, nullptr, 0);
}

bool ThreadSuspender::SuspendAllThreads() {
  int pterrno;
  if (internal_iserror(internal_ptrace(PT_ATTACH, pid_, nullptr, 0),
                       &pterrno)) {
    Printf("Could not attach to process %d (errno %d).\n", pid_, pterrno);
    return false;
  }

  int status;
  uptr waitpid_status;
  HANDLE_EINTR(waitpid_status, internal_waitpid(pid_, &status, 0));

  VReport(2, "Attached to process %d.\n", pid_);

#ifdef PT_LWPNEXT
  struct ptrace_lwpstatus pl;
  int op = PT_LWPNEXT;
#else
  struct ptrace_lwpinfo pl;
  int op = PT_LWPINFO;
#endif

  pl.pl_lwpid = 0;

  int val;
  while ((val = internal_ptrace(op, pid_, (void *)&pl, sizeof(pl))) != -1 &&
         pl.pl_lwpid != 0) {
    suspended_threads_list_.Append(pl.pl_lwpid);
    VReport(2, "Appended thread %d in process %d.\n", pl.pl_lwpid, pid_);
  }
  return true;
}

// Pointer to the ThreadSuspender instance for use in signal handler.
static ThreadSuspender *thread_suspender_instance = nullptr;

// Synchronous signals that should not be blocked.
static const int kSyncSignals[] = {SIGABRT, SIGILL,  SIGFPE, SIGSEGV,
                                   SIGBUS,  SIGXCPU, SIGXFSZ};

static void TracerThreadDieCallback() {
  ThreadSuspender *inst = thread_suspender_instance;
  if (inst && stoptheworld_tracer_pid == internal_getpid()) {
    inst->KillAllThreads();
    thread_suspender_instance = nullptr;
  }
}

// Signal handler to wake up suspended threads when the tracer thread dies.
static void TracerThreadSignalHandler(int signum, __sanitizer_siginfo *siginfo,
                                      void *uctx) {
  SignalContext ctx(siginfo, uctx);
  Printf("Tracer caught signal %d: addr=%p pc=%p sp=%p\n", signum,
         (void *)ctx.addr, (void *)ctx.pc, (void *)ctx.sp);
  ThreadSuspender *inst = thread_suspender_instance;
  if (inst) {
    if (signum == SIGABRT)
      inst->KillAllThreads();
    else
      inst->ResumeAllThreads();
    RAW_CHECK(RemoveDieCallback(TracerThreadDieCallback));
    thread_suspender_instance = nullptr;
    atomic_store(&inst->arg->done, 1, memory_order_relaxed);
  }
  internal__exit((signum == SIGABRT) ? 1 : 2);
}

// Size of alternative stack for signal handlers in the tracer thread.
static const int kHandlerStackSize = 8192;

// This function will be run as a cloned task.
static int TracerThread(void *argument) {
  TracerThreadArgument *tracer_thread_argument =
      (TracerThreadArgument *)argument;

  // Check if parent is already dead.
  if (internal_getppid() != tracer_thread_argument->parent_pid)
    internal__exit(4);

  // Wait for the parent thread to finish preparations.
  tracer_thread_argument->mutex.Lock();
  tracer_thread_argument->mutex.Unlock();

  RAW_CHECK(AddDieCallback(TracerThreadDieCallback));

  ThreadSuspender thread_suspender(internal_getppid(), tracer_thread_argument);
  // Global pointer for the signal handler.
  thread_suspender_instance = &thread_suspender;

  // Alternate stack for signal handling.
  InternalMmapVector<char> handler_stack_memory(kHandlerStackSize);
  stack_t handler_stack;
  internal_memset(&handler_stack, 0, sizeof(handler_stack));
  handler_stack.ss_sp = handler_stack_memory.data();
  handler_stack.ss_size = kHandlerStackSize;
  internal_sigaltstack(&handler_stack, nullptr);

  // Install our handler for synchronous signals. Other signals should be
  // blocked by the mask we inherited from the parent thread.
  for (uptr i = 0; i < ARRAY_SIZE(kSyncSignals); i++) {
    __sanitizer_sigaction act;
    internal_memset(&act, 0, sizeof(act));
    act.sigaction = TracerThreadSignalHandler;
    act.sa_flags = SA_ONSTACK | SA_SIGINFO;
    internal_sigaction_norestorer(kSyncSignals[i], &act, 0);
  }

  int exit_code = 0;
  if (!thread_suspender.SuspendAllThreads()) {
    VReport(1, "Failed suspending threads.\n");
    exit_code = 3;
  } else {
    tracer_thread_argument->callback(thread_suspender.suspended_threads_list(),
                                     tracer_thread_argument->callback_argument);
    thread_suspender.ResumeAllThreads();
    exit_code = 0;
  }
  RAW_CHECK(RemoveDieCallback(TracerThreadDieCallback));
  thread_suspender_instance = nullptr;
  atomic_store(&tracer_thread_argument->done, 1, memory_order_relaxed);
  return exit_code;
}

class ScopedStackSpaceWithGuard {
 public:
  explicit ScopedStackSpaceWithGuard(uptr stack_size) {
    stack_size_ = stack_size;
    guard_size_ = GetPageSizeCached();
    // FIXME: Omitting MAP_STACK here works in current kernels but might break
    // in the future.
    guard_start_ =
        (uptr)MmapOrDie(stack_size_ + guard_size_, "ScopedStackWithGuard");
    CHECK(MprotectNoAccess((uptr)guard_start_, guard_size_));
  }
  ~ScopedStackSpaceWithGuard() {
    UnmapOrDie((void *)guard_start_, stack_size_ + guard_size_);
  }
  void *Bottom() const {
    return (void *)(guard_start_ + stack_size_ + guard_size_);
  }

 private:
  uptr stack_size_;
  uptr guard_size_;
  uptr guard_start_;
};

static __sanitizer_sigset_t blocked_sigset;
static __sanitizer_sigset_t old_sigset;

struct ScopedSetTracerPID {
  explicit ScopedSetTracerPID(uptr tracer_pid) {
    stoptheworld_tracer_pid = tracer_pid;
    stoptheworld_tracer_ppid = internal_getpid();
  }
  ~ScopedSetTracerPID() {
    stoptheworld_tracer_pid = 0;
    stoptheworld_tracer_ppid = 0;
  }
};

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  // Prepare the arguments for TracerThread.
  struct TracerThreadArgument tracer_thread_argument;
  tracer_thread_argument.callback = callback;
  tracer_thread_argument.callback_argument = argument;
  tracer_thread_argument.parent_pid = internal_getpid();
  atomic_store(&tracer_thread_argument.done, 0, memory_order_relaxed);
  const uptr kTracerStackSize = 2 * 1024 * 1024;
  ScopedStackSpaceWithGuard tracer_stack(kTracerStackSize);

  tracer_thread_argument.mutex.Lock();

  internal_sigfillset(&blocked_sigset);
  for (uptr i = 0; i < ARRAY_SIZE(kSyncSignals); i++)
    internal_sigdelset(&blocked_sigset, kSyncSignals[i]);
  int rv = internal_sigprocmask(SIG_BLOCK, &blocked_sigset, &old_sigset);
  CHECK_EQ(rv, 0);
  uptr tracer_pid = internal_clone(TracerThread, tracer_stack.Bottom(),
                                   CLONE_VM | CLONE_FS | CLONE_FILES,
                                   &tracer_thread_argument);
  internal_sigprocmask(SIG_SETMASK, &old_sigset, 0);
  int local_errno = 0;
  if (internal_iserror(tracer_pid, &local_errno)) {
    VReport(1, "Failed spawning a tracer thread (errno %d).\n", local_errno);
    tracer_thread_argument.mutex.Unlock();
  } else {
    ScopedSetTracerPID scoped_set_tracer_pid(tracer_pid);

    tracer_thread_argument.mutex.Unlock();

    while (atomic_load(&tracer_thread_argument.done, memory_order_relaxed) == 0)
      sched_yield();

    for (;;) {
      uptr waitpid_status = internal_waitpid(tracer_pid, nullptr, __WALL);
      if (!internal_iserror(waitpid_status, &local_errno))
        break;
      if (local_errno == EINTR)
        continue;
      VReport(1, "Waiting on the tracer thread failed (errno %d).\n",
              local_errno);
      break;
    }
  }
}

tid_t SuspendedThreadsListNetBSD::GetThreadID(uptr index) const {
  CHECK_LT(index, thread_ids_.size());
  return thread_ids_[index];
}

uptr SuspendedThreadsListNetBSD::ThreadCount() const {
  return thread_ids_.size();
}

bool SuspendedThreadsListNetBSD::ContainsTid(tid_t thread_id) const {
  for (uptr i = 0; i < thread_ids_.size(); i++) {
    if (thread_ids_[i] == thread_id)
      return true;
  }
  return false;
}

void SuspendedThreadsListNetBSD::Append(tid_t tid) {
  thread_ids_.push_back(tid);
}

PtraceRegistersStatus SuspendedThreadsListNetBSD::GetRegistersAndSP(
    uptr index, InternalMmapVector<uptr> *buffer, uptr *sp) const {
  lwpid_t tid = GetThreadID(index);
  pid_t ppid = internal_getppid();
  struct reg regs;
  int pterrno;
  bool isErr =
      internal_iserror(internal_ptrace(PT_GETREGS, ppid, &regs, tid), &pterrno);
  if (isErr) {
    VReport(1,
            "Could not get registers from process %d thread %d (errno %d).\n",
            ppid, tid, pterrno);
    return pterrno == ESRCH ? REGISTERS_UNAVAILABLE_FATAL
                            : REGISTERS_UNAVAILABLE;
  }

  *sp = PTRACE_REG_SP(&regs);
  buffer->resize(RoundUpTo(sizeof(regs), sizeof(uptr)) / sizeof(uptr));
  internal_memcpy(buffer->data(), &regs, sizeof(regs));

  return REGISTERS_AVAILABLE;
}

}  // namespace __sanitizer

#endif
