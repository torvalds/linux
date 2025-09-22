//===-- sanitizer_stoptheworld_linux_libcdep.cpp --------------------------===//
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
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_LINUX &&                                                   \
    (defined(__x86_64__) || defined(__mips__) || defined(__aarch64__) || \
     defined(__powerpc64__) || defined(__s390__) || defined(__i386__) || \
     defined(__arm__) || SANITIZER_RISCV64 || SANITIZER_LOONGARCH64)

#include "sanitizer_stoptheworld.h"

#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_atomic.h"

#include <errno.h>
#include <sched.h> // for CLONE_* definitions
#include <stddef.h>
#include <sys/prctl.h> // for PR_* definitions
#include <sys/ptrace.h> // for PTRACE_* definitions
#include <sys/types.h> // for pid_t
#include <sys/uio.h> // for iovec
#include <elf.h> // for NT_PRSTATUS
#if (defined(__aarch64__) || SANITIZER_RISCV64 || SANITIZER_LOONGARCH64) && \
     !SANITIZER_ANDROID
// GLIBC 2.20+ sys/user does not include asm/ptrace.h
# include <asm/ptrace.h>
#endif
#include <sys/user.h>  // for user_regs_struct
#if SANITIZER_ANDROID && SANITIZER_MIPS
# include <asm/reg.h>  // for mips SP register in sys/user.h
#endif
#include <sys/wait.h> // for signal-related stuff

#ifdef sa_handler
# undef sa_handler
#endif

#ifdef sa_sigaction
# undef sa_sigaction
#endif

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_linux.h"
#include "sanitizer_mutex.h"
#include "sanitizer_placement_new.h"

// Sufficiently old kernel headers don't provide this value, but we can still
// call prctl with it. If the runtime kernel is new enough, the prctl call will
// have the desired effect; if the kernel is too old, the call will error and we
// can ignore said error.
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

// This module works by spawning a Linux task which then attaches to every
// thread in the caller process with ptrace. This suspends the threads, and
// PTRACE_GETREGS can then be used to obtain their register state. The callback
// supplied to StopTheWorld() is run in the tracer task while the threads are
// suspended.
// The tracer task must be placed in a different thread group for ptrace to
// work, so it cannot be spawned as a pthread. Instead, we use the low-level
// clone() interface (we want to share the address space with the caller
// process, so we prefer clone() over fork()).
//
// We don't use any libc functions, relying instead on direct syscalls. There
// are two reasons for this:
// 1. calling a library function while threads are suspended could cause a
// deadlock, if one of the treads happens to be holding a libc lock;
// 2. it's generally not safe to call libc functions from the tracer task,
// because clone() does not set up a thread-local storage for it. Any
// thread-local variables used by libc will be shared between the tracer task
// and the thread which spawned it.

namespace __sanitizer {

class SuspendedThreadsListLinux final : public SuspendedThreadsList {
 public:
  SuspendedThreadsListLinux() { thread_ids_.reserve(1024); }

  tid_t GetThreadID(uptr index) const override;
  uptr ThreadCount() const override;
  bool ContainsTid(tid_t thread_id) const;
  void Append(tid_t tid);

  PtraceRegistersStatus GetRegistersAndSP(uptr index,
                                          InternalMmapVector<uptr> *buffer,
                                          uptr *sp) const override;

 private:
  InternalMmapVector<tid_t> thread_ids_;
};

// Structure for passing arguments into the tracer thread.
struct TracerThreadArgument {
  StopTheWorldCallback callback;
  void *callback_argument;
  // The tracer thread waits on this mutex while the parent finishes its
  // preparations.
  Mutex mutex;
  // Tracer thread signals its completion by setting done.
  atomic_uintptr_t done;
  uptr parent_pid;
};

// This class handles thread suspending/unsuspending in the tracer thread.
class ThreadSuspender {
 public:
  explicit ThreadSuspender(pid_t pid, TracerThreadArgument *arg)
    : arg(arg)
    , pid_(pid) {
      CHECK_GE(pid, 0);
    }
  bool SuspendAllThreads();
  void ResumeAllThreads();
  void KillAllThreads();
  SuspendedThreadsListLinux &suspended_threads_list() {
    return suspended_threads_list_;
  }
  TracerThreadArgument *arg;
 private:
  SuspendedThreadsListLinux suspended_threads_list_;
  pid_t pid_;
  bool SuspendThread(tid_t thread_id);
};

bool ThreadSuspender::SuspendThread(tid_t tid) {
  // Are we already attached to this thread?
  // Currently this check takes linear time, however the number of threads is
  // usually small.
  if (suspended_threads_list_.ContainsTid(tid)) return false;
  int pterrno;
  if (internal_iserror(internal_ptrace(PTRACE_ATTACH, tid, nullptr, nullptr),
                       &pterrno)) {
    // Either the thread is dead, or something prevented us from attaching.
    // Log this event and move on.
    VReport(1, "Could not attach to thread %zu (errno %d).\n", (uptr)tid,
            pterrno);
    return false;
  } else {
    VReport(2, "Attached to thread %zu.\n", (uptr)tid);
    // The thread is not guaranteed to stop before ptrace returns, so we must
    // wait on it. Note: if the thread receives a signal concurrently,
    // we can get notification about the signal before notification about stop.
    // In such case we need to forward the signal to the thread, otherwise
    // the signal will be missed (as we do PTRACE_DETACH with arg=0) and
    // any logic relying on signals will break. After forwarding we need to
    // continue to wait for stopping, because the thread is not stopped yet.
    // We do ignore delivery of SIGSTOP, because we want to make stop-the-world
    // as invisible as possible.
    for (;;) {
      int status;
      uptr waitpid_status;
      HANDLE_EINTR(waitpid_status, internal_waitpid(tid, &status, __WALL));
      int wperrno;
      if (internal_iserror(waitpid_status, &wperrno)) {
        // Got a ECHILD error. I don't think this situation is possible, but it
        // doesn't hurt to report it.
        VReport(1, "Waiting on thread %zu failed, detaching (errno %d).\n",
                (uptr)tid, wperrno);
        internal_ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
        return false;
      }
      if (WIFSTOPPED(status) && WSTOPSIG(status) != SIGSTOP) {
        internal_ptrace(PTRACE_CONT, tid, nullptr,
                        (void*)(uptr)WSTOPSIG(status));
        continue;
      }
      break;
    }
    suspended_threads_list_.Append(tid);
    return true;
  }
}

void ThreadSuspender::ResumeAllThreads() {
  for (uptr i = 0; i < suspended_threads_list_.ThreadCount(); i++) {
    pid_t tid = suspended_threads_list_.GetThreadID(i);
    int pterrno;
    if (!internal_iserror(internal_ptrace(PTRACE_DETACH, tid, nullptr, nullptr),
                          &pterrno)) {
      VReport(2, "Detached from thread %d.\n", tid);
    } else {
      // Either the thread is dead, or we are already detached.
      // The latter case is possible, for instance, if this function was called
      // from a signal handler.
      VReport(1, "Could not detach from thread %d (errno %d).\n", tid, pterrno);
    }
  }
}

void ThreadSuspender::KillAllThreads() {
  for (uptr i = 0; i < suspended_threads_list_.ThreadCount(); i++)
    internal_ptrace(PTRACE_KILL, suspended_threads_list_.GetThreadID(i),
                    nullptr, nullptr);
}

bool ThreadSuspender::SuspendAllThreads() {
  ThreadLister thread_lister(pid_);
  bool retry = true;
  InternalMmapVector<tid_t> threads;
  threads.reserve(128);
  for (int i = 0; i < 30 && retry; ++i) {
    retry = false;
    switch (thread_lister.ListThreads(&threads)) {
      case ThreadLister::Error:
        ResumeAllThreads();
        return false;
      case ThreadLister::Incomplete:
        retry = true;
        break;
      case ThreadLister::Ok:
        break;
    }
    for (tid_t tid : threads) {
      if (SuspendThread(tid))
        retry = true;
    }
  }
  return suspended_threads_list_.ThreadCount();
}

// Pointer to the ThreadSuspender instance for use in signal handler.
static ThreadSuspender *thread_suspender_instance = nullptr;

// Synchronous signals that should not be blocked.
static const int kSyncSignals[] = { SIGABRT, SIGILL, SIGFPE, SIGSEGV, SIGBUS,
                                    SIGXCPU, SIGXFSZ };

static void TracerThreadDieCallback() {
  // Generally a call to Die() in the tracer thread should be fatal to the
  // parent process as well, because they share the address space.
  // This really only works correctly if all the threads are suspended at this
  // point. So we correctly handle calls to Die() from within the callback, but
  // not those that happen before or after the callback. Hopefully there aren't
  // a lot of opportunities for that to happen...
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
static int TracerThread(void* argument) {
  TracerThreadArgument *tracer_thread_argument =
      (TracerThreadArgument *)argument;

  internal_prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
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
    guard_start_ = (uptr)MmapOrDie(stack_size_ + guard_size_,
                                   "ScopedStackWithGuard");
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

// We have a limitation on the stack frame size, so some stuff had to be moved
// into globals.
static __sanitizer_sigset_t blocked_sigset;
static __sanitizer_sigset_t old_sigset;

class StopTheWorldScope {
 public:
  StopTheWorldScope() {
    // Make this process dumpable. Processes that are not dumpable cannot be
    // attached to.
    process_was_dumpable_ = internal_prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
    if (!process_was_dumpable_)
      internal_prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
  }

  ~StopTheWorldScope() {
    // Restore the dumpable flag.
    if (!process_was_dumpable_)
      internal_prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
  }

 private:
  int process_was_dumpable_;
};

// When sanitizer output is being redirected to file (i.e. by using log_path),
// the tracer should write to the parent's log instead of trying to open a new
// file. Alert the logging code to the fact that we have a tracer.
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
  StopTheWorldScope in_stoptheworld;
  // Prepare the arguments for TracerThread.
  struct TracerThreadArgument tracer_thread_argument;
  tracer_thread_argument.callback = callback;
  tracer_thread_argument.callback_argument = argument;
  tracer_thread_argument.parent_pid = internal_getpid();
  atomic_store(&tracer_thread_argument.done, 0, memory_order_relaxed);
  const uptr kTracerStackSize = 2 * 1024 * 1024;
  ScopedStackSpaceWithGuard tracer_stack(kTracerStackSize);
  // Block the execution of TracerThread until after we have set ptrace
  // permissions.
  tracer_thread_argument.mutex.Lock();
  // Signal handling story.
  // We don't want async signals to be delivered to the tracer thread,
  // so we block all async signals before creating the thread. An async signal
  // handler can temporary modify errno, which is shared with this thread.
  // We ought to use pthread_sigmask here, because sigprocmask has undefined
  // behavior in multithreaded programs. However, on linux sigprocmask is
  // equivalent to pthread_sigmask with the exception that pthread_sigmask
  // does not allow to block some signals used internally in pthread
  // implementation. We are fine with blocking them here, we are really not
  // going to pthread_cancel the thread.
  // The tracer thread should not raise any synchronous signals. But in case it
  // does, we setup a special handler for sync signals that properly kills the
  // parent as well. Note: we don't pass CLONE_SIGHAND to clone, so handlers
  // in the tracer thread won't interfere with user program. Double note: if a
  // user does something along the lines of 'kill -11 pid', that can kill the
  // process even if user setup own handler for SEGV.
  // Thing to watch out for: this code should not change behavior of user code
  // in any observable way. In particular it should not override user signal
  // handlers.
  internal_sigfillset(&blocked_sigset);
  for (uptr i = 0; i < ARRAY_SIZE(kSyncSignals); i++)
    internal_sigdelset(&blocked_sigset, kSyncSignals[i]);
  int rv = internal_sigprocmask(SIG_BLOCK, &blocked_sigset, &old_sigset);
  CHECK_EQ(rv, 0);
  uptr tracer_pid = internal_clone(
      TracerThread, tracer_stack.Bottom(),
      CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_UNTRACED,
      &tracer_thread_argument, nullptr /* parent_tidptr */,
      nullptr /* newtls */, nullptr /* child_tidptr */);
  internal_sigprocmask(SIG_SETMASK, &old_sigset, 0);
  int local_errno = 0;
  if (internal_iserror(tracer_pid, &local_errno)) {
    VReport(1, "Failed spawning a tracer thread (errno %d).\n", local_errno);
    tracer_thread_argument.mutex.Unlock();
  } else {
    ScopedSetTracerPID scoped_set_tracer_pid(tracer_pid);
    // On some systems we have to explicitly declare that we want to be traced
    // by the tracer thread.
    internal_prctl(PR_SET_PTRACER, tracer_pid, 0, 0, 0);
    // Allow the tracer thread to start.
    tracer_thread_argument.mutex.Unlock();
    // NOTE: errno is shared between this thread and the tracer thread.
    // internal_waitpid() may call syscall() which can access/spoil errno,
    // so we can't call it now. Instead we for the tracer thread to finish using
    // the spin loop below. Man page for sched_yield() says "In the Linux
    // implementation, sched_yield() always succeeds", so let's hope it does not
    // spoil errno. Note that this spin loop runs only for brief periods before
    // the tracer thread has suspended us and when it starts unblocking threads.
    while (atomic_load(&tracer_thread_argument.done, memory_order_relaxed) == 0)
      sched_yield();
    // Now the tracer thread is about to exit and does not touch errno,
    // wait for it.
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

// Platform-specific methods from SuspendedThreadsList.
#if SANITIZER_ANDROID && defined(__arm__)
typedef pt_regs regs_struct;
#define REG_SP ARM_sp

#elif SANITIZER_LINUX && defined(__arm__)
typedef user_regs regs_struct;
#define REG_SP uregs[13]

#elif defined(__i386__) || defined(__x86_64__)
typedef user_regs_struct regs_struct;
#if defined(__i386__)
#define REG_SP esp
#else
#define REG_SP rsp
#endif
#define ARCH_IOVEC_FOR_GETREGSET
// Support ptrace extensions even when compiled without required kernel support
#ifndef NT_X86_XSTATE
#define NT_X86_XSTATE 0x202
#endif
#ifndef PTRACE_GETREGSET
#define PTRACE_GETREGSET 0x4204
#endif
// Compiler may use FP registers to store pointers.
static constexpr uptr kExtraRegs[] = {NT_X86_XSTATE, NT_FPREGSET};

#elif defined(__powerpc__) || defined(__powerpc64__)
typedef pt_regs regs_struct;
#define REG_SP gpr[PT_R1]

#elif defined(__mips__)
typedef struct user regs_struct;
# if SANITIZER_ANDROID
#  define REG_SP regs[EF_R29]
# else
#  define REG_SP regs[EF_REG29]
# endif

#elif defined(__aarch64__)
typedef struct user_pt_regs regs_struct;
#define REG_SP sp
static constexpr uptr kExtraRegs[] = {0};
#define ARCH_IOVEC_FOR_GETREGSET

#elif defined(__loongarch__)
typedef struct user_pt_regs regs_struct;
#define REG_SP regs[3]
static constexpr uptr kExtraRegs[] = {0};
#define ARCH_IOVEC_FOR_GETREGSET

#elif SANITIZER_RISCV64
typedef struct user_regs_struct regs_struct;
// sys/ucontext.h already defines REG_SP as 2. Undefine it first.
#undef REG_SP
#define REG_SP sp
static constexpr uptr kExtraRegs[] = {0};
#define ARCH_IOVEC_FOR_GETREGSET

#elif defined(__s390__)
typedef _user_regs_struct regs_struct;
#define REG_SP gprs[15]
static constexpr uptr kExtraRegs[] = {0};
#define ARCH_IOVEC_FOR_GETREGSET

#else
#error "Unsupported architecture"
#endif // SANITIZER_ANDROID && defined(__arm__)

tid_t SuspendedThreadsListLinux::GetThreadID(uptr index) const {
  CHECK_LT(index, thread_ids_.size());
  return thread_ids_[index];
}

uptr SuspendedThreadsListLinux::ThreadCount() const {
  return thread_ids_.size();
}

bool SuspendedThreadsListLinux::ContainsTid(tid_t thread_id) const {
  for (uptr i = 0; i < thread_ids_.size(); i++) {
    if (thread_ids_[i] == thread_id) return true;
  }
  return false;
}

void SuspendedThreadsListLinux::Append(tid_t tid) {
  thread_ids_.push_back(tid);
}

PtraceRegistersStatus SuspendedThreadsListLinux::GetRegistersAndSP(
    uptr index, InternalMmapVector<uptr> *buffer, uptr *sp) const {
  pid_t tid = GetThreadID(index);
  constexpr uptr uptr_sz = sizeof(uptr);
  int pterrno;
#ifdef ARCH_IOVEC_FOR_GETREGSET
  auto AppendF = [&](uptr regset) {
    uptr size = buffer->size();
    // NT_X86_XSTATE requires 64bit alignment.
    uptr size_up = RoundUpTo(size, 8 / uptr_sz);
    buffer->reserve(Max<uptr>(1024, size_up));
    struct iovec regset_io;
    for (;; buffer->resize(buffer->capacity() * 2)) {
      buffer->resize(buffer->capacity());
      uptr available_bytes = (buffer->size() - size_up) * uptr_sz;
      regset_io.iov_base = buffer->data() + size_up;
      regset_io.iov_len = available_bytes;
      bool fail =
          internal_iserror(internal_ptrace(PTRACE_GETREGSET, tid,
                                           (void *)regset, (void *)&regset_io),
                           &pterrno);
      if (fail) {
        VReport(1, "Could not get regset %p from thread %d (errno %d).\n",
                (void *)regset, tid, pterrno);
        buffer->resize(size);
        return false;
      }

      // Far enough from the buffer size, no need to resize and repeat.
      if (regset_io.iov_len + 64 < available_bytes)
        break;
    }
    buffer->resize(size_up + RoundUpTo(regset_io.iov_len, uptr_sz) / uptr_sz);
    return true;
  };

  buffer->clear();
  bool fail = !AppendF(NT_PRSTATUS);
  if (!fail) {
    // Accept the first available and do not report errors.
    for (uptr regs : kExtraRegs)
      if (regs && AppendF(regs))
        break;
  }
#else
  buffer->resize(RoundUpTo(sizeof(regs_struct), uptr_sz) / uptr_sz);
  bool fail = internal_iserror(
      internal_ptrace(PTRACE_GETREGS, tid, nullptr, buffer->data()), &pterrno);
  if (fail)
    VReport(1, "Could not get registers from thread %d (errno %d).\n", tid,
            pterrno);
#endif
  if (fail) {
    // ESRCH means that the given thread is not suspended or already dead.
    // Therefore it's unsafe to inspect its data (e.g. walk through stack) and
    // we should notify caller about this.
    return pterrno == ESRCH ? REGISTERS_UNAVAILABLE_FATAL
                            : REGISTERS_UNAVAILABLE;
  }

  *sp = reinterpret_cast<regs_struct *>(buffer->data())[0].REG_SP;
  return REGISTERS_AVAILABLE;
}

} // namespace __sanitizer

#endif  // SANITIZER_LINUX && (defined(__x86_64__) || defined(__mips__)
        // || defined(__aarch64__) || defined(__powerpc64__)
        // || defined(__s390__) || defined(__i386__) || defined(__arm__)
        // || SANITIZER_LOONGARCH64
