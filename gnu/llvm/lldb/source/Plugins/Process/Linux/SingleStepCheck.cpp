//===-- SingleStepCheck.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SingleStepCheck.h"

#include <csignal>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include "NativeProcessLinux.h"

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errno.h"

#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Host/linux/Ptrace.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_linux;

#if defined(__arm64__) || defined(__aarch64__)
namespace {

[[noreturn]] void Child() {
  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1)
    _exit(1);

  // We just do an endless loop SIGSTOPPING ourselves until killed. The tracer
  // will fiddle with our cpu affinities and monitor the behaviour.
  for (;;) {
    raise(SIGSTOP);

    // Generate a bunch of instructions here, so that a single-step does not
    // land in the raise() accidentally. If single-stepping works, we will be
    // spinning in this loop. If it doesn't, we'll land in the raise() call
    // above.
    for (volatile unsigned i = 0; i < CPU_SETSIZE; ++i)
      ;
  }
}

struct ChildDeleter {
  ::pid_t pid;

  ~ChildDeleter() {
    int status;
    // Kill the child.
    kill(pid, SIGKILL);
    // Pick up the remains.
    llvm::sys::RetryAfterSignal(-1, waitpid, pid, &status, __WALL);
  }
};

bool WorkaroundNeeded() {
  // We shall spawn a child, and use it to verify the debug capabilities of the
  // cpu. We shall iterate through the cpus, bind the child to each one in
  // turn, and verify that single-stepping works on that cpu. A workaround is
  // needed if we find at least one broken cpu.

  Log *log = GetLog(POSIXLog::Thread);
  ::pid_t child_pid = fork();
  if (child_pid == -1) {
    LLDB_LOG(log, "failed to fork(): {0}", Status(errno, eErrorTypePOSIX));
    return false;
  }
  if (child_pid == 0)
    Child();

  ChildDeleter child_deleter{child_pid};
  cpu_set_t available_cpus;
  if (sched_getaffinity(child_pid, sizeof available_cpus, &available_cpus) ==
      -1) {
    LLDB_LOG(log, "failed to get available cpus: {0}",
             Status(errno, eErrorTypePOSIX));
    return false;
  }

  int status;
  ::pid_t wpid = llvm::sys::RetryAfterSignal(-1, waitpid,
      child_pid, &status, __WALL);
  if (wpid != child_pid || !WIFSTOPPED(status)) {
    LLDB_LOG(log, "waitpid() failed (status = {0:x}): {1}", status,
             Status(errno, eErrorTypePOSIX));
    return false;
  }

  unsigned cpu;
  for (cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &available_cpus))
      continue;

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(cpu, &cpus);
    if (sched_setaffinity(child_pid, sizeof cpus, &cpus) == -1) {
      LLDB_LOG(log, "failed to switch to cpu {0}: {1}", cpu,
               Status(errno, eErrorTypePOSIX));
      continue;
    }

    int status;
    Status error =
        NativeProcessLinux::PtraceWrapper(PTRACE_SINGLESTEP, child_pid);
    if (error.Fail()) {
      LLDB_LOG(log, "single step failed: {0}", error);
      break;
    }

    wpid = llvm::sys::RetryAfterSignal(-1, waitpid,
        child_pid, &status, __WALL);
    if (wpid != child_pid || !WIFSTOPPED(status)) {
      LLDB_LOG(log, "waitpid() failed (status = {0:x}): {1}", status,
               Status(errno, eErrorTypePOSIX));
      break;
    }
    if (WSTOPSIG(status) != SIGTRAP) {
      LLDB_LOG(log, "single stepping on cpu {0} failed with status {1:x}", cpu,
               status);
      break;
    }
  }

  // cpu is either the index of the first broken cpu, or CPU_SETSIZE.
  if (cpu == 0) {
    LLDB_LOG(log,
             "SINGLE STEPPING ON FIRST CPU IS NOT WORKING. DEBUGGING "
             "LIKELY TO BE UNRELIABLE.");
    // No point in trying to fiddle with the affinities, just give it our best
    // shot and see how it goes.
    return false;
  }

  return cpu != CPU_SETSIZE;
}

} // end anonymous namespace

std::unique_ptr<SingleStepWorkaround> SingleStepWorkaround::Get(::pid_t tid) {
  Log *log = GetLog(POSIXLog::Thread);

  static bool workaround_needed = WorkaroundNeeded();
  if (!workaround_needed) {
    LLDB_LOG(log, "workaround for thread {0} not needed", tid);
    return nullptr;
  }

  cpu_set_t original_set;
  if (sched_getaffinity(tid, sizeof original_set, &original_set) != 0) {
    // This should really not fail. But, just in case...
    LLDB_LOG(log, "Unable to get cpu affinity for thread {0}: {1}", tid,
             Status(errno, eErrorTypePOSIX));
    return nullptr;
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  if (sched_setaffinity(tid, sizeof set, &set) != 0) {
    // This may fail in very locked down systems, if the thread is not allowed
    // to run on cpu 0. If that happens, only thing we can do is it log it and
    // continue...
    LLDB_LOG(log, "Unable to set cpu affinity for thread {0}: {1}", tid,
             Status(errno, eErrorTypePOSIX));
  }

  LLDB_LOG(log, "workaround for thread {0} prepared", tid);
  return std::make_unique<SingleStepWorkaround>(tid, original_set);
}

SingleStepWorkaround::~SingleStepWorkaround() {
  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "Removing workaround");
  if (sched_setaffinity(m_tid, sizeof m_original_set, &m_original_set) != 0) {
    LLDB_LOG(log, "Unable to reset cpu affinity for thread {0}: {1}", m_tid,
             Status(errno, eErrorTypePOSIX));
  }
}
#endif
