//===-- NativeProcessOpenBSD.cpp ------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeProcessOpenBSD.h"

// C Includes
#include <errno.h>
#define _DYN_LOADER
#include <elf.h>
#include <util.h>

// C++ Includes

#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#include "llvm/Support/Errno.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/State.h"

// System includes - They have to be included after framework includes because
// they define some macros which collide with variable names in other modules
// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
// clang-format on

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_openbsd;
using namespace llvm;

// Simple helper function to ensure flags are enabled on the given file
// descriptor.
static Status EnsureFDFlags(int fd, int flags) {
  Status error;

  int status = fcntl(fd, F_GETFL);
  if (status == -1) {
    error.SetErrorToErrno();
    return error;
  }

  if (fcntl(fd, F_SETFL, status | flags) == -1) {
    error.SetErrorToErrno();
    return error;
  }

  return error;
}

// -----------------------------------------------------------------------------
// Public Static Methods
// -----------------------------------------------------------------------------

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessOpenBSD::Manager::Launch(ProcessLaunchInfo &launch_info,
                                      NativeDelegate &native_delegate) {
  Log *log = GetLog(POSIXLog::Process);

  Status status;
  ::pid_t pid = ProcessLauncherPosixFork()
                    .LaunchProcess(launch_info, status)
                    .GetProcessId();
  LLDB_LOG(log, "pid = {0:x}", pid);
  if (status.Fail()) {
    LLDB_LOG(log, "failed to launch process: {0}", status);
    return status.ToError();
  }

  // Wait for the child process to trap on its call to execve.
  int wstatus;
  ::pid_t wpid = llvm::sys::RetryAfterSignal(-1, ::waitpid, pid, &wstatus, 0);
  assert(wpid == pid);
  (void)wpid;
  if (!WIFSTOPPED(wstatus)) {
    LLDB_LOG(log, "Could not sync with inferior process: wstatus={1}",
             WaitStatus::Decode(wstatus));
    return llvm::make_error<StringError>("Could not sync with inferior process",
                                         llvm::inconvertibleErrorCode());
  }
  LLDB_LOG(log, "inferior started, now in stopped state");

  ProcessInstanceInfo Info;
  if (!Host::GetProcessInfo(pid, Info)) {
    return llvm::make_error<StringError>("Cannot get process architecture",
                                         llvm::inconvertibleErrorCode());
  }

  // Set the architecture to the exe architecture.
  LLDB_LOG(log, "pid = {0:x}, detected architecture {1}", pid,
           Info.GetArchitecture().GetArchitectureName());

  std::unique_ptr<NativeProcessOpenBSD> process_up(new NativeProcessOpenBSD(
      pid, launch_info.GetPTY().ReleasePrimaryFileDescriptor(), native_delegate,
      Info.GetArchitecture(), m_mainloop));

  status = process_up->ReinitializeThreads();
  if (status.Fail())
    return status.ToError();

  for (const auto &thread : process_up->m_threads)
    static_cast<NativeThreadOpenBSD &>(*thread).SetStoppedBySignal(SIGSTOP);
  process_up->SetState(StateType::eStateStopped, false);

  return std::move(process_up);
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessOpenBSD::Manager::Attach(
    lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate) {

  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid = {0:x}", pid);

  // Retrieve the architecture for the running process.
  ProcessInstanceInfo Info;
  if (!Host::GetProcessInfo(pid, Info)) {
    return llvm::make_error<StringError>("Cannot get process architecture",
                                         llvm::inconvertibleErrorCode());
  }

  std::unique_ptr<NativeProcessOpenBSD> process_up(new NativeProcessOpenBSD(
      pid, -1, native_delegate, Info.GetArchitecture(), m_mainloop));

  Status status = process_up->Attach();
  if (!status.Success())
    return status.ToError();

  return std::move(process_up);
}

NativeProcessOpenBSD::Extension
NativeProcessOpenBSD::Manager::GetSupportedExtensions() const {
    return Extension::multiprocess | Extension::fork | Extension::vfork |
           Extension::pass_signals | Extension::auxv | Extension::libraries_svr4;
}


// -----------------------------------------------------------------------------
// Public Instance Methods
// -----------------------------------------------------------------------------

NativeProcessOpenBSD::NativeProcessOpenBSD(::pid_t pid, int terminal_fd,
                                         NativeDelegate &delegate,
                                         const ArchSpec &arch,
					 MainLoop &mainloop)
    : NativeProcessProtocol(pid, terminal_fd, delegate), m_arch(arch),
      m_main_loop(mainloop) {
  if (m_terminal_fd != -1) {
    Status status = EnsureFDFlags(m_terminal_fd, O_NONBLOCK);
    assert(status.Success());
  }

  Status status;
  m_sigchld_handle = mainloop.RegisterSignal(
      SIGCHLD, [this](MainLoopBase &) { SigchldHandler(); }, status);
  assert(m_sigchld_handle && status.Success());
}

// Handles all waitpid events from the inferior process.
void NativeProcessOpenBSD::MonitorCallback(lldb::pid_t pid, int signal) {
  switch (signal) {
    case SIGTRAP:
      return MonitorSIGTRAP(pid);
    default:
      return MonitorSignal(pid, signal);
  }
}

void NativeProcessOpenBSD::MonitorExited(lldb::pid_t pid, WaitStatus status) {
  Log *log = GetLog(POSIXLog::Process);

  LLDB_LOG(log, "got exit signal({0}) , pid = {1}", status, pid);

  /* Stop Tracking All Threads attached to Process */
  m_threads.clear();

  SetExitStatus(status, true);

  // Notify delegate that our process has exited.
  SetState(StateType::eStateExited, true);
}

void NativeProcessOpenBSD::MonitorSIGTRAP(lldb::pid_t pid) {
  for (const auto &thread : m_threads) {
    static_cast<NativeThreadOpenBSD &>(*thread).SetStoppedByBreakpoint();
    FixupBreakpointPCAsNeeded(static_cast<NativeThreadOpenBSD &>(*thread));
  }
  SetState(StateType::eStateStopped, true);
}


void NativeProcessOpenBSD::MonitorSignal(lldb::pid_t pid, int signal) {
  for (const auto &thread : m_threads) {
    static_cast<NativeThreadOpenBSD &>(*thread).SetStoppedBySignal(signal);
  }
  SetState(StateType::eStateStopped, true);
}

Status NativeProcessOpenBSD::PtraceWrapper(int req, lldb::pid_t pid, void *addr,
                                          int data, int *result) {
  Log *log = GetLog(POSIXLog::Process);
  Status error;
  int ret;

  errno = 0;
  ret = ptrace(req, static_cast<::pid_t>(pid), (caddr_t)addr, data);

  if (ret == -1)
    error.SetErrorToErrno();

  if (result)
    *result = ret;

  LLDB_LOG(log, "ptrace({0}, {1}, {2}, {3})={4:x}", req, pid, addr, data, ret);

  if (error.Fail())
    LLDB_LOG(log, "ptrace() failed: {0}", error);

  return error;
}

Status NativeProcessOpenBSD::Resume(const ResumeActionList &resume_actions) {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid {0}", GetID());

  const auto &thread = m_threads[0];
  const ResumeAction *const action =
      resume_actions.GetActionForThread(thread->GetID(), true);

  if (action == nullptr) {
    LLDB_LOG(log, "no action specified for pid {0} tid {1}", GetID(),
             thread->GetID());
    return Status();
  }

  Status error;
  int signal = action->signal != LLDB_INVALID_SIGNAL_NUMBER ? action->signal : 0;

  switch (action->state) {
  case eStateRunning: {
    // Run the thread, possibly feeding it the signal.
    error = NativeProcessOpenBSD::PtraceWrapper(PT_CONTINUE, GetID(), (void *)1,
                                               signal);
    if (!error.Success())
      return error;
    for (const auto &thread : m_threads)
      static_cast<NativeThreadOpenBSD &>(*thread).SetRunning();
    SetState(eStateRunning, true);
    break;
  }
  case eStateStepping:
#ifdef PT_STEP
    // Run the thread, possibly feeding it the signal.
    error = NativeProcessOpenBSD::PtraceWrapper(PT_STEP, GetID(), (void *)1,
                                               signal);
    if (!error.Success())
      return error;
    for (const auto &thread : m_threads)
      static_cast<NativeThreadOpenBSD &>(*thread).SetStepping();
    SetState(eStateStepping, true);
    break;
#else
    return Status("NativeProcessOpenBSD stepping not supported on this platform");
#endif

  case eStateSuspended:
  case eStateStopped:
    llvm_unreachable("Unexpected state");

  default:
    return Status("NativeProcessOpenBSD::%s (): unexpected state %s specified "
                  "for pid %" PRIu64 ", tid %" PRIu64,
                  __FUNCTION__, StateAsCString(action->state), GetID(),
                  thread->GetID());
  }

  return Status();
}

Status NativeProcessOpenBSD::Halt() {
  Status error;

  if (kill(GetID(), SIGSTOP) != 0)
    error.SetErrorToErrno();

  return error;
}

Status NativeProcessOpenBSD::Detach() {
  Status error;

  // Stop monitoring the inferior.
  m_sigchld_handle.reset();

  // Tell ptrace to detach from the process.
  if (GetID() == LLDB_INVALID_PROCESS_ID)
    return error;

  return PtraceWrapper(PT_DETACH, GetID());
}

Status NativeProcessOpenBSD::Signal(int signo) {
  Status error;

  if (kill(GetID(), signo))
    error.SetErrorToErrno();

  return error;
}

Status NativeProcessOpenBSD::Kill() {
  Log *log = GetLog(POSIXLog::Process);
  LLDB_LOG(log, "pid {0}", GetID());

  Status error;

  switch (m_state) {
  case StateType::eStateInvalid:
  case StateType::eStateExited:
  case StateType::eStateCrashed:
  case StateType::eStateDetached:
  case StateType::eStateUnloaded:
    // Nothing to do - the process is already dead.
    LLDB_LOG(log, "ignored for PID {0} due to current state: {1}", GetID(),
             StateAsCString(m_state));
    return error;

  case StateType::eStateConnected:
  case StateType::eStateAttaching:
  case StateType::eStateLaunching:
  case StateType::eStateStopped:
  case StateType::eStateRunning:
  case StateType::eStateStepping:
  case StateType::eStateSuspended:
    // We can try to kill a process in these states.
    break;
  }

  if (kill(GetID(), SIGKILL) != 0) {
    error.SetErrorToErrno();
    return error;
  }

  return error;
}

Status NativeProcessOpenBSD::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                                MemoryRegionInfo &range_info) {
  return Status("Unimplemented");
}

Status NativeProcessOpenBSD::PopulateMemoryRegionCache() {
  return Status("Unimplemented");
}

llvm::Expected<lldb::addr_t> NativeProcessOpenBSD::AllocateMemory(size_t size,
                                                                 uint32_t permissions) {
  return llvm::make_error<UnimplementedError>();
}

llvm::Error NativeProcessOpenBSD::DeallocateMemory(lldb::addr_t addr) {
  return llvm::make_error<UnimplementedError>();
}

lldb::addr_t NativeProcessOpenBSD::GetSharedLibraryInfoAddress() {
  // punt on this for now
  return LLDB_INVALID_ADDRESS;
}

size_t NativeProcessOpenBSD::UpdateThreads() { return m_threads.size(); }

Status NativeProcessOpenBSD::SetBreakpoint(lldb::addr_t addr, uint32_t size,
                                          bool hardware) {
  if (hardware)
    return Status("NativeProcessOpenBSD does not support hardware breakpoints");
  else
    return SetSoftwareBreakpoint(addr, size);
}

Status NativeProcessOpenBSD::GetLoadedModuleFileSpec(const char *module_path,
                                                    FileSpec &file_spec) {
  return Status("Unimplemented");
}

Status NativeProcessOpenBSD::GetFileLoadAddress(const llvm::StringRef &file_name,
                                               lldb::addr_t &load_addr) {
  load_addr = LLDB_INVALID_ADDRESS;
  return Status();
}

void NativeProcessOpenBSD::SigchldHandler() {
  Log *log = GetLog(POSIXLog::Process);
  // Process all pending waitpid notifications.
  int status;
  ::pid_t wait_pid =
      llvm::sys::RetryAfterSignal(-1, waitpid, GetID(), &status, WNOHANG);

  if (wait_pid == 0)
    return; // We are done.

  if (wait_pid == -1) {
    Status error(errno, eErrorTypePOSIX);
    LLDB_LOG(log, "waitpid ({0}, &status, _) failed: {1}", GetID(), error);
  }

  WaitStatus wait_status = WaitStatus::Decode(status);
  bool exited = wait_status.type == WaitStatus::Exit ||
                (wait_status.type == WaitStatus::Signal &&
                 wait_pid == static_cast<::pid_t>(GetID()));

  LLDB_LOG(log,
           "waitpid ({0}, &status, _) => pid = {1}, status = {2}, exited = {3}",
           GetID(), wait_pid, status, exited);

  if (exited)
    MonitorExited(wait_pid, wait_status);
  else {
    assert(wait_status.type == WaitStatus::Stop);
    MonitorCallback(wait_pid, wait_status.status);
  }
}

bool NativeProcessOpenBSD::HasThreadNoLock(lldb::tid_t thread_id) {
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not contain NULL threads");
    if (thread->GetID() == thread_id) {
      // We have this thread.
      return true;
    }
  }

  // We don't have this thread.
  return false;
}

NativeThreadOpenBSD &NativeProcessOpenBSD::AddThread(lldb::tid_t thread_id) {

  Log *log = GetLog(POSIXLog::Thread);
  LLDB_LOG(log, "pid {0} adding thread with tid {1}", GetID(), thread_id);

  assert(!HasThreadNoLock(thread_id) &&
         "attempted to add a thread by id that already exists");

  // If this is the first thread, save it as the current thread
  if (m_threads.empty())
    SetCurrentThreadID(thread_id);

  m_threads.push_back(std::make_unique<NativeThreadOpenBSD>(*this, thread_id));
  return static_cast<NativeThreadOpenBSD &>(*m_threads.back());
}

Status NativeProcessOpenBSD::Attach() {
  // Attach to the requested process.
  // An attach will cause the thread to stop with a SIGSTOP.
  Status status = PtraceWrapper(PT_ATTACH, m_pid);
  if (status.Fail())
    return status;

  int wstatus;
  // At this point we should have a thread stopped if waitpid succeeds.
  if ((wstatus = waitpid(m_pid, NULL, 0)) < 0)
    return Status(errno, eErrorTypePOSIX);

  /* Initialize threads */
  status = ReinitializeThreads();
  if (status.Fail())
    return status;

  for (const auto &thread : m_threads)
    static_cast<NativeThreadOpenBSD &>(*thread).SetStoppedBySignal(SIGSTOP);

  // Let our process instance know the thread has stopped.
  SetState(StateType::eStateStopped);
  return Status();
}

Status NativeProcessOpenBSD::ReadMemory(lldb::addr_t addr, void *buf,
                                       size_t size, size_t &bytes_read) {
  unsigned char *dst = static_cast<unsigned char *>(buf);
  struct ptrace_io_desc io;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  bytes_read = 0;
  io.piod_op = PIOD_READ_D;
  io.piod_len = size;

  do {
    io.piod_offs = (void *)(addr + bytes_read);
    io.piod_addr = dst + bytes_read;

    Status error = NativeProcessOpenBSD::PtraceWrapper(PT_IO, GetID(), &io);
    if (error.Fail())
      return error;

    bytes_read = io.piod_len;
    io.piod_len = size - bytes_read;
  } while (bytes_read < size);

  return Status();
}

Status NativeProcessOpenBSD::WriteMemory(lldb::addr_t addr, const void *buf,
                                        size_t size, size_t &bytes_written) {
  const unsigned char *src = static_cast<const unsigned char *>(buf);
  Status error;
  struct ptrace_io_desc io;

  Log *log = GetLog(POSIXLog::Memory);
  LLDB_LOG(log, "addr = {0}, buf = {1}, size = {2}", addr, buf, size);

  bytes_written = 0;
  io.piod_op = PIOD_WRITE_D;
  io.piod_len = size;

  do {
    io.piod_addr = const_cast<void *>(static_cast<const void *>(src + bytes_written));
    io.piod_offs = (void *)(addr + bytes_written);

    Status error = NativeProcessOpenBSD::PtraceWrapper(PT_IO, GetID(), &io);
    if (error.Fail())
      return error;

    bytes_written = io.piod_len;
    io.piod_len = size - bytes_written;
  } while (bytes_written < size);

  return error;
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
NativeProcessOpenBSD::GetAuxvData() const {
  size_t auxv_size = 100 * sizeof(AuxInfo);

  ErrorOr<std::unique_ptr<WritableMemoryBuffer>> buf =
      llvm::WritableMemoryBuffer::getNewMemBuffer(auxv_size);

  struct ptrace_io_desc io;
  io.piod_op = PIOD_READ_AUXV;
  io.piod_offs = 0;
  io.piod_addr = static_cast<void *>(buf.get()->getBufferStart());
  io.piod_len = auxv_size;

  Status error = NativeProcessOpenBSD::PtraceWrapper(PT_IO, GetID(), &io);

  if (error.Fail())
    return std::error_code(error.GetError(), std::generic_category());

  if (io.piod_len < 1)
    return std::error_code(ECANCELED, std::generic_category());

  return std::move(buf);
}

Status NativeProcessOpenBSD::ReinitializeThreads() {
  // Clear old threads
  m_threads.clear();

  // Initialize new thread
  struct ptrace_thread_state info = {};
  Status error = PtraceWrapper(PT_GET_THREAD_FIRST, GetID(), &info, sizeof(info));
  if (error.Fail()) {
    return error;
  }
  // Reinitialize from scratch threads and register them in process
  while (info.pts_tid > 0) {
    AddThread(info.pts_tid);
    error = PtraceWrapper(PT_GET_THREAD_NEXT, GetID(), &info, sizeof(info));
    if (error.Fail() && errno != 0) {
      return error;
    }
  }

  return error;
}
