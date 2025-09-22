//===- FuzzerUtilFuchsia.cpp - Misc utils for Fuchsia. --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Misc utils implementation using Fuchsia/Zircon APIs.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"

#if LIBFUZZER_FUCHSIA

#include "FuzzerInternal.h"
#include "FuzzerUtil.h"
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/spawn.h>
#include <string>
#include <sys/select.h>
#include <thread>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <vector>

namespace fuzzer {

// Given that Fuchsia doesn't have the POSIX signals that libFuzzer was written
// around, the general approach is to spin up dedicated threads to watch for
// each requested condition (alarm, interrupt, crash).  Of these, the crash
// handler is the most involved, as it requires resuming the crashed thread in
// order to invoke the sanitizers to get the needed state.

// Forward declaration of assembly trampoline needed to resume crashed threads.
// This appears to have external linkage to  C++, which is why it's not in the
// anonymous namespace.  The assembly definition inside MakeTrampoline()
// actually defines the symbol with internal linkage only.
void CrashTrampolineAsm() __asm__("CrashTrampolineAsm");

namespace {

// The signal handler thread uses Zircon exceptions to resume crashed threads
// into libFuzzer's POSIX signal handlers. The associated event is used to
// signal when the thread is running, and when it should stop.
std::thread SignalHandler;
zx_handle_t SignalHandlerEvent = ZX_HANDLE_INVALID;

// Helper function to handle Zircon syscall failures.
void ExitOnErr(zx_status_t Status, const char *Syscall) {
  if (Status != ZX_OK) {
    Printf("libFuzzer: %s failed: %s\n", Syscall,
           _zx_status_get_string(Status));
    exit(1);
  }
}

void AlarmHandler(int Seconds) {
  while (true) {
    SleepSeconds(Seconds);
    Fuzzer::StaticAlarmCallback();
  }
}

// For the crash handler, we need to call Fuzzer::StaticCrashSignalCallback
// without POSIX signal handlers.  To achieve this, we use an assembly function
// to add the necessary CFI unwinding information and a C function to bridge
// from that back into C++.

// FIXME: This works as a short-term solution, but this code really shouldn't be
// architecture dependent. A better long term solution is to implement remote
// unwinding and expose the necessary APIs through sanitizer_common and/or ASAN
// to allow the exception handling thread to gather the crash state directly.
//
// Alternatively, Fuchsia may in future actually implement basic signal
// handling for the machine trap signals.
#if defined(__x86_64__)

#define FOREACH_REGISTER(OP_REG, OP_NUM) \
  OP_REG(rax)                            \
  OP_REG(rbx)                            \
  OP_REG(rcx)                            \
  OP_REG(rdx)                            \
  OP_REG(rsi)                            \
  OP_REG(rdi)                            \
  OP_REG(rbp)                            \
  OP_REG(rsp)                            \
  OP_REG(r8)                             \
  OP_REG(r9)                             \
  OP_REG(r10)                            \
  OP_REG(r11)                            \
  OP_REG(r12)                            \
  OP_REG(r13)                            \
  OP_REG(r14)                            \
  OP_REG(r15)                            \
  OP_REG(rip)

#elif defined(__aarch64__)

#define FOREACH_REGISTER(OP_REG, OP_NUM) \
  OP_NUM(0)                              \
  OP_NUM(1)                              \
  OP_NUM(2)                              \
  OP_NUM(3)                              \
  OP_NUM(4)                              \
  OP_NUM(5)                              \
  OP_NUM(6)                              \
  OP_NUM(7)                              \
  OP_NUM(8)                              \
  OP_NUM(9)                              \
  OP_NUM(10)                             \
  OP_NUM(11)                             \
  OP_NUM(12)                             \
  OP_NUM(13)                             \
  OP_NUM(14)                             \
  OP_NUM(15)                             \
  OP_NUM(16)                             \
  OP_NUM(17)                             \
  OP_NUM(18)                             \
  OP_NUM(19)                             \
  OP_NUM(20)                             \
  OP_NUM(21)                             \
  OP_NUM(22)                             \
  OP_NUM(23)                             \
  OP_NUM(24)                             \
  OP_NUM(25)                             \
  OP_NUM(26)                             \
  OP_NUM(27)                             \
  OP_NUM(28)                             \
  OP_NUM(29)                             \
  OP_REG(sp)

#elif defined(__riscv)

#define FOREACH_REGISTER(OP_REG, OP_NUM)                                      \
  OP_REG(ra)                                                                  \
  OP_REG(sp)                                                                  \
  OP_REG(gp)                                                                  \
  OP_REG(tp)                                                                  \
  OP_REG(t0)                                                                  \
  OP_REG(t1)                                                                  \
  OP_REG(t2)                                                                  \
  OP_REG(s0)                                                                  \
  OP_REG(s1)                                                                  \
  OP_REG(a0)                                                                  \
  OP_REG(a1)                                                                  \
  OP_REG(a2)                                                                  \
  OP_REG(a3)                                                                  \
  OP_REG(a4)                                                                  \
  OP_REG(a5)                                                                  \
  OP_REG(a6)                                                                  \
  OP_REG(a7)                                                                  \
  OP_REG(s2)                                                                  \
  OP_REG(s3)                                                                  \
  OP_REG(s4)                                                                  \
  OP_REG(s5)                                                                  \
  OP_REG(s6)                                                                  \
  OP_REG(s7)                                                                  \
  OP_REG(s8)                                                                  \
  OP_REG(s9)                                                                  \
  OP_REG(s10)                                                                 \
  OP_REG(s11)                                                                 \
  OP_REG(t3)                                                                  \
  OP_REG(t4)                                                                  \
  OP_REG(t5)                                                                  \
  OP_REG(t6)                                                                  \

#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif

// Produces a CFI directive for the named or numbered register.
// The value used refers to an assembler immediate operand with the same name
// as the register (see ASM_OPERAND_REG).
#define CFI_OFFSET_REG(reg) ".cfi_offset " #reg ", %c[" #reg "]\n"
#define CFI_OFFSET_NUM(num) CFI_OFFSET_REG(x##num)

// Produces an assembler immediate operand for the named or numbered register.
// This operand contains the offset of the register relative to the CFA.
#define ASM_OPERAND_REG(reg)                                                   \
  [reg] "i"(offsetof(zx_thread_state_general_regs_t, reg)),
#define ASM_OPERAND_NUM(num)                                                   \
  [x##num] "i"(offsetof(zx_thread_state_general_regs_t, r[num])),

// Trampoline to bridge from the assembly below to the static C++ crash
// callback.
__attribute__((noreturn))
static void StaticCrashHandler() {
  Fuzzer::StaticCrashSignalCallback();
  for (;;) {
    _Exit(1);
  }
}

// This trampoline function has the necessary CFI information to unwind
// and get a backtrace:
//  * The stack contains a copy of all the registers at the point of crash,
//    the code has CFI directives specifying how to restore them.
//  * A call to StaticCrashHandler, which will print the stacktrace and exit
//    the fuzzer, generating a crash artifact.
//
// The __attribute__((used)) is necessary because the function
// is never called; it's just a container around the assembly to allow it to
// use operands for compile-time computed constants.
__attribute__((used))
void MakeTrampoline() {
  __asm__(
      ".cfi_endproc\n"
      ".pushsection .text.CrashTrampolineAsm\n"
      ".type CrashTrampolineAsm,STT_FUNC\n"
      "CrashTrampolineAsm:\n"
      ".cfi_startproc simple\n"
      ".cfi_signal_frame\n"
#if defined(__x86_64__)
      ".cfi_return_column rip\n"
      ".cfi_def_cfa rsp, 0\n"
      FOREACH_REGISTER(CFI_OFFSET_REG, CFI_OFFSET_NUM)
      "call %c[StaticCrashHandler]\n"
      "ud2\n"
#elif defined(__aarch64__)
      ".cfi_return_column 33\n"
      ".cfi_def_cfa sp, 0\n"
      FOREACH_REGISTER(CFI_OFFSET_REG, CFI_OFFSET_NUM)
      ".cfi_offset 33, %c[pc]\n"
      ".cfi_offset 30, %c[lr]\n"
      "bl %c[StaticCrashHandler]\n"
      "brk 1\n"
#elif defined(__riscv)
      ".cfi_return_column 64\n"
      ".cfi_def_cfa sp, 0\n"
      ".cfi_offset 64, %[pc]\n"
      FOREACH_REGISTER(CFI_OFFSET_REG, CFI_OFFSET_NUM)
      "call %c[StaticCrashHandler]\n"
      "unimp\n"
#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif
     ".cfi_endproc\n"
     ".size CrashTrampolineAsm, . - CrashTrampolineAsm\n"
     ".popsection\n"
     ".cfi_startproc\n"
      : // No outputs
      : FOREACH_REGISTER(ASM_OPERAND_REG, ASM_OPERAND_NUM)
#if defined(__aarch64__) || defined(__riscv)
        ASM_OPERAND_REG(pc)
#endif
#if defined(__aarch64__)
        ASM_OPERAND_REG(lr)
#endif
        [StaticCrashHandler] "i"(StaticCrashHandler));
}

void CrashHandler() {
  assert(SignalHandlerEvent != ZX_HANDLE_INVALID);

  // This structure is used to ensure we close handles to objects we create in
  // this handler.
  struct ScopedHandle {
    ~ScopedHandle() { _zx_handle_close(Handle); }
    zx_handle_t Handle = ZX_HANDLE_INVALID;
  };

  // Create the exception channel.  We need to claim to be a "debugger" so the
  // kernel will allow us to modify and resume dying threads (see below). Once
  // the channel is set, we can signal the main thread to continue and wait
  // for the exception to arrive.
  ScopedHandle Channel;
  zx_handle_t Self = _zx_process_self();
  ExitOnErr(_zx_task_create_exception_channel(
                Self, ZX_EXCEPTION_CHANNEL_DEBUGGER, &Channel.Handle),
            "_zx_task_create_exception_channel");

  ExitOnErr(_zx_object_signal(SignalHandlerEvent, 0, ZX_USER_SIGNAL_0),
            "_zx_object_signal");

  // This thread lives as long as the process in order to keep handling
  // crashes.  In practice, the first crashed thread to reach the end of the
  // StaticCrashHandler will end the process.
  while (true) {
    zx_wait_item_t WaitItems[] = {
        {
            .handle = SignalHandlerEvent,
            .waitfor = ZX_USER_SIGNAL_1,
            .pending = 0,
        },
        {
            .handle = Channel.Handle,
            .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            .pending = 0,
        },
    };
    auto Status = _zx_object_wait_many(
        WaitItems, sizeof(WaitItems) / sizeof(WaitItems[0]), ZX_TIME_INFINITE);
    if (Status != ZX_OK || (WaitItems[1].pending & ZX_CHANNEL_READABLE) == 0) {
      break;
    }

    zx_exception_info_t ExceptionInfo;
    ScopedHandle Exception;
    ExitOnErr(_zx_channel_read(Channel.Handle, 0, &ExceptionInfo,
                               &Exception.Handle, sizeof(ExceptionInfo), 1,
                               nullptr, nullptr),
              "_zx_channel_read");

    // Ignore informational synthetic exceptions.
    if (ZX_EXCP_THREAD_STARTING == ExceptionInfo.type ||
        ZX_EXCP_THREAD_EXITING == ExceptionInfo.type ||
        ZX_EXCP_PROCESS_STARTING == ExceptionInfo.type) {
      continue;
    }

    // At this point, we want to get the state of the crashing thread, but
    // libFuzzer and the sanitizers assume this will happen from that same
    // thread via a POSIX signal handler. "Resurrecting" the thread in the
    // middle of the appropriate callback is as simple as forcibly setting the
    // instruction pointer/program counter, provided we NEVER EVER return from
    // that function (since otherwise our stack will not be valid).
    ScopedHandle Thread;
    ExitOnErr(_zx_exception_get_thread(Exception.Handle, &Thread.Handle),
              "_zx_exception_get_thread");

    zx_thread_state_general_regs_t GeneralRegisters;
    ExitOnErr(_zx_thread_read_state(Thread.Handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &GeneralRegisters,
                                    sizeof(GeneralRegisters)),
              "_zx_thread_read_state");

    // To unwind properly, we need to push the crashing thread's register state
    // onto the stack and jump into a trampoline with CFI instructions on how
    // to restore it.
#if defined(__x86_64__)

    uintptr_t StackPtr =
        (GeneralRegisters.rsp - (128 + sizeof(GeneralRegisters))) &
        -(uintptr_t)16;
    __unsanitized_memcpy(reinterpret_cast<void *>(StackPtr), &GeneralRegisters,
                         sizeof(GeneralRegisters));
    GeneralRegisters.rsp = StackPtr;
    GeneralRegisters.rip = reinterpret_cast<zx_vaddr_t>(CrashTrampolineAsm);

#elif defined(__aarch64__) || defined(__riscv)

    uintptr_t StackPtr =
        (GeneralRegisters.sp - sizeof(GeneralRegisters)) & -(uintptr_t)16;
    __unsanitized_memcpy(reinterpret_cast<void *>(StackPtr), &GeneralRegisters,
                         sizeof(GeneralRegisters));
    GeneralRegisters.sp = StackPtr;
    GeneralRegisters.pc = reinterpret_cast<zx_vaddr_t>(CrashTrampolineAsm);

#else
#error "Unsupported architecture for fuzzing on Fuchsia"
#endif

    // Now force the crashing thread's state.
    ExitOnErr(
        _zx_thread_write_state(Thread.Handle, ZX_THREAD_STATE_GENERAL_REGS,
                               &GeneralRegisters, sizeof(GeneralRegisters)),
        "_zx_thread_write_state");

    // Set the exception to HANDLED so it resumes the thread on close.
    uint32_t ExceptionState = ZX_EXCEPTION_STATE_HANDLED;
    ExitOnErr(_zx_object_set_property(Exception.Handle, ZX_PROP_EXCEPTION_STATE,
                                      &ExceptionState, sizeof(ExceptionState)),
              "zx_object_set_property");
  }
}

void StopSignalHandler() {
  _zx_object_signal(SignalHandlerEvent, 0, ZX_USER_SIGNAL_1);
  if (SignalHandler.joinable()) {
    SignalHandler.join();
  }
  _zx_handle_close(SignalHandlerEvent);
}

} // namespace

// Platform specific functions.
void SetSignalHandler(const FuzzingOptions &Options) {
  // Make sure information from libFuzzer and the sanitizers are easy to
  // reassemble. `__sanitizer_log_write` has the added benefit of ensuring the
  // DSO map is always available for the symbolizer.
  // A uint64_t fits in 20 chars, so 64 is plenty.
  char Buf[64];
  memset(Buf, 0, sizeof(Buf));
  snprintf(Buf, sizeof(Buf), "==%lu== INFO: libFuzzer starting.\n", GetPid());
  if (EF->__sanitizer_log_write)
    __sanitizer_log_write(Buf, sizeof(Buf));
  Printf("%s", Buf);

  // Set up alarm handler if needed.
  if (Options.HandleAlrm && Options.UnitTimeoutSec > 0) {
    std::thread T(AlarmHandler, Options.UnitTimeoutSec / 2 + 1);
    T.detach();
  }

  // Options.HandleInt and Options.HandleTerm are not supported on Fuchsia

  // Early exit if no crash handler needed.
  if (!Options.HandleSegv && !Options.HandleBus && !Options.HandleIll &&
      !Options.HandleFpe && !Options.HandleAbrt)
    return;

  // Set up the crash handler and wait until it is ready before proceeding.
  ExitOnErr(_zx_event_create(0, &SignalHandlerEvent), "_zx_event_create");

  SignalHandler = std::thread(CrashHandler);
  zx_status_t Status = _zx_object_wait_one(SignalHandlerEvent, ZX_USER_SIGNAL_0,
                                           ZX_TIME_INFINITE, nullptr);
  ExitOnErr(Status, "_zx_object_wait_one");

  std::atexit(StopSignalHandler);
}

void SleepSeconds(int Seconds) {
  _zx_nanosleep(_zx_deadline_after(ZX_SEC(Seconds)));
}

unsigned long GetPid() {
  zx_status_t rc;
  zx_info_handle_basic_t Info;
  if ((rc = _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &Info,
                                sizeof(Info), NULL, NULL)) != ZX_OK) {
    Printf("libFuzzer: unable to get info about self: %s\n",
           _zx_status_get_string(rc));
    exit(1);
  }
  return Info.koid;
}

size_t GetPeakRSSMb() {
  zx_status_t rc;
  zx_info_task_stats_t Info;
  if ((rc = _zx_object_get_info(_zx_process_self(), ZX_INFO_TASK_STATS, &Info,
                                sizeof(Info), NULL, NULL)) != ZX_OK) {
    Printf("libFuzzer: unable to get info about self: %s\n",
           _zx_status_get_string(rc));
    exit(1);
  }
  return (Info.mem_private_bytes + Info.mem_shared_bytes) >> 20;
}

template <typename Fn>
class RunOnDestruction {
 public:
  explicit RunOnDestruction(Fn fn) : fn_(fn) {}
  ~RunOnDestruction() { fn_(); }

 private:
  Fn fn_;
};

template <typename Fn>
RunOnDestruction<Fn> at_scope_exit(Fn fn) {
  return RunOnDestruction<Fn>(fn);
}

static fdio_spawn_action_t clone_fd_action(int localFd, int targetFd) {
  return {
      .action = FDIO_SPAWN_ACTION_CLONE_FD,
      .fd =
          {
              .local_fd = localFd,
              .target_fd = targetFd,
          },
  };
}

int ExecuteCommand(const Command &Cmd) {
  zx_status_t rc;

  // Convert arguments to C array
  auto Args = Cmd.getArguments();
  size_t Argc = Args.size();
  assert(Argc != 0);
  std::unique_ptr<const char *[]> Argv(new const char *[Argc + 1]);
  for (size_t i = 0; i < Argc; ++i)
    Argv[i] = Args[i].c_str();
  Argv[Argc] = nullptr;

  // Determine output.  On Fuchsia, the fuzzer is typically run as a component
  // that lacks a mutable working directory. Fortunately, when this is the case
  // a mutable output directory must be specified using "-artifact_prefix=...",
  // so write the log file(s) there.
  // However, we don't want to apply this logic for absolute paths.
  int FdOut = STDOUT_FILENO;
  bool discardStdout = false;
  bool discardStderr = false;

  if (Cmd.hasOutputFile()) {
    std::string Path = Cmd.getOutputFile();
    if (Path == getDevNull()) {
      // On Fuchsia, there's no "/dev/null" like-file, so we
      // just don't copy the FDs into the spawned process.
      discardStdout = true;
    } else {
      bool IsAbsolutePath = Path.length() > 1 && Path[0] == '/';
      if (!IsAbsolutePath && Cmd.hasFlag("artifact_prefix"))
        Path = Cmd.getFlagValue("artifact_prefix") + "/" + Path;

      FdOut = open(Path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0);
      if (FdOut == -1) {
        Printf("libFuzzer: failed to open %s: %s\n", Path.c_str(),
               strerror(errno));
        return ZX_ERR_IO;
      }
    }
  }
  auto CloseFdOut = at_scope_exit([FdOut]() {
    if (FdOut != STDOUT_FILENO)
      close(FdOut);
  });

  // Determine stderr
  int FdErr = STDERR_FILENO;
  if (Cmd.isOutAndErrCombined()) {
    FdErr = FdOut;
    if (discardStdout)
      discardStderr = true;
  }

  // Clone the file descriptors into the new process
  std::vector<fdio_spawn_action_t> SpawnActions;
  SpawnActions.push_back(clone_fd_action(STDIN_FILENO, STDIN_FILENO));

  if (!discardStdout)
    SpawnActions.push_back(clone_fd_action(FdOut, STDOUT_FILENO));
  if (!discardStderr)
    SpawnActions.push_back(clone_fd_action(FdErr, STDERR_FILENO));

  // Start the process.
  char ErrorMsg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_handle_t ProcessHandle = ZX_HANDLE_INVALID;
  rc = fdio_spawn_etc(ZX_HANDLE_INVALID,
                      FDIO_SPAWN_CLONE_ALL & (~FDIO_SPAWN_CLONE_STDIO), Argv[0],
                      Argv.get(), nullptr, SpawnActions.size(),
                      SpawnActions.data(), &ProcessHandle, ErrorMsg);

  if (rc != ZX_OK) {
    Printf("libFuzzer: failed to launch '%s': %s, %s\n", Argv[0], ErrorMsg,
           _zx_status_get_string(rc));
    return rc;
  }
  auto CloseHandle = at_scope_exit([&]() { _zx_handle_close(ProcessHandle); });

  // Now join the process and return the exit status.
  if ((rc = _zx_object_wait_one(ProcessHandle, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, nullptr)) != ZX_OK) {
    Printf("libFuzzer: failed to join '%s': %s\n", Argv[0],
           _zx_status_get_string(rc));
    return rc;
  }

  zx_info_process_t Info;
  if ((rc = _zx_object_get_info(ProcessHandle, ZX_INFO_PROCESS, &Info,
                                sizeof(Info), nullptr, nullptr)) != ZX_OK) {
    Printf("libFuzzer: unable to get return code from '%s': %s\n", Argv[0],
           _zx_status_get_string(rc));
    return rc;
  }

  return static_cast<int>(Info.return_code);
}

bool ExecuteCommand(const Command &BaseCmd, std::string *CmdOutput) {
  auto LogFilePath = TempPath("SimPopenOut", ".txt");
  Command Cmd(BaseCmd);
  Cmd.setOutputFile(LogFilePath);
  int Ret = ExecuteCommand(Cmd);
  *CmdOutput = FileToString(LogFilePath);
  RemoveFile(LogFilePath);
  return Ret == 0;
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  return memmem(Data, DataLen, Patt, PattLen);
}

// In fuchsia, accessing /dev/null is not supported. There's nothing
// similar to a file that discards everything that is written to it.
// The way of doing something similar in fuchsia is by using
// fdio_null_create and binding that to a file descriptor.
void DiscardOutput(int Fd) {
  fdio_t *fdio_null = fdio_null_create();
  if (fdio_null == nullptr) return;
  int nullfd = fdio_bind_to_fd(fdio_null, -1, 0);
  if (nullfd < 0) return;
  dup2(nullfd, Fd);
}

size_t PageSize() {
  static size_t PageSizeCached = _zx_system_get_page_size();
  return PageSizeCached;
}

void SetThreadName(std::thread &thread, const std::string &name) {
  // TODO ?
}

} // namespace fuzzer

#endif // LIBFUZZER_FUCHSIA
