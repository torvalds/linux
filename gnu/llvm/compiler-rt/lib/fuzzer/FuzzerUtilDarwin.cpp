//===- FuzzerUtilDarwin.cpp - Misc utils ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Misc utils for Darwin.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"
#if LIBFUZZER_APPLE
#include "FuzzerCommand.h"
#include "FuzzerIO.h"
#include <mutex>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// There is no header for this on macOS so declare here
extern "C" char **environ;

namespace fuzzer {

static std::mutex SignalMutex;
// Global variables used to keep track of how signal handling should be
// restored. They should **not** be accessed without holding `SignalMutex`.
static int ActiveThreadCount = 0;
static struct sigaction OldSigIntAction;
static struct sigaction OldSigQuitAction;
static sigset_t OldBlockedSignalsSet;

// This is a reimplementation of Libc's `system()`. On Darwin the Libc
// implementation contains a mutex which prevents it from being used
// concurrently. This implementation **can** be used concurrently. It sets the
// signal handlers when the first thread enters and restores them when the last
// thread finishes execution of the function and ensures this is not racey by
// using a mutex.
int ExecuteCommand(const Command &Cmd) {
  std::string CmdLine = Cmd.toString();
  posix_spawnattr_t SpawnAttributes;
  if (posix_spawnattr_init(&SpawnAttributes))
    return -1;
  // Block and ignore signals of the current process when the first thread
  // enters.
  {
    std::lock_guard<std::mutex> Lock(SignalMutex);
    if (ActiveThreadCount == 0) {
      static struct sigaction IgnoreSignalAction;
      sigset_t BlockedSignalsSet;
      memset(&IgnoreSignalAction, 0, sizeof(IgnoreSignalAction));
      IgnoreSignalAction.sa_handler = SIG_IGN;

      if (sigaction(SIGINT, &IgnoreSignalAction, &OldSigIntAction) == -1) {
        Printf("Failed to ignore SIGINT\n");
        (void)posix_spawnattr_destroy(&SpawnAttributes);
        return -1;
      }
      if (sigaction(SIGQUIT, &IgnoreSignalAction, &OldSigQuitAction) == -1) {
        Printf("Failed to ignore SIGQUIT\n");
        // Try our best to restore the signal handlers.
        (void)sigaction(SIGINT, &OldSigIntAction, NULL);
        (void)posix_spawnattr_destroy(&SpawnAttributes);
        return -1;
      }

      (void)sigemptyset(&BlockedSignalsSet);
      (void)sigaddset(&BlockedSignalsSet, SIGCHLD);
      if (sigprocmask(SIG_BLOCK, &BlockedSignalsSet, &OldBlockedSignalsSet) ==
          -1) {
        Printf("Failed to block SIGCHLD\n");
        // Try our best to restore the signal handlers.
        (void)sigaction(SIGQUIT, &OldSigQuitAction, NULL);
        (void)sigaction(SIGINT, &OldSigIntAction, NULL);
        (void)posix_spawnattr_destroy(&SpawnAttributes);
        return -1;
      }
    }
    ++ActiveThreadCount;
  }

  // NOTE: Do not introduce any new `return` statements past this
  // point. It is important that `ActiveThreadCount` always be decremented
  // when leaving this function.

  // Make sure the child process uses the default handlers for the
  // following signals rather than inheriting what the parent has.
  sigset_t DefaultSigSet;
  (void)sigemptyset(&DefaultSigSet);
  (void)sigaddset(&DefaultSigSet, SIGQUIT);
  (void)sigaddset(&DefaultSigSet, SIGINT);
  (void)posix_spawnattr_setsigdefault(&SpawnAttributes, &DefaultSigSet);
  // Make sure the child process doesn't block SIGCHLD
  (void)posix_spawnattr_setsigmask(&SpawnAttributes, &OldBlockedSignalsSet);
  short SpawnFlags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
  (void)posix_spawnattr_setflags(&SpawnAttributes, SpawnFlags);

  pid_t Pid;
  char **Environ = environ; // Read from global
  const char *CommandCStr = CmdLine.c_str();
  char *const Argv[] = {
    strdup("sh"),
    strdup("-c"),
    strdup(CommandCStr),
    NULL
  };
  int ErrorCode = 0, ProcessStatus = 0;
  // FIXME: We probably shouldn't hardcode the shell path.
  ErrorCode = posix_spawn(&Pid, "/bin/sh", NULL, &SpawnAttributes,
                          Argv, Environ);
  (void)posix_spawnattr_destroy(&SpawnAttributes);
  if (!ErrorCode) {
    pid_t SavedPid = Pid;
    do {
      // Repeat until call completes uninterrupted.
      Pid = waitpid(SavedPid, &ProcessStatus, /*options=*/0);
    } while (Pid == -1 && errno == EINTR);
    if (Pid == -1) {
      // Fail for some other reason.
      ProcessStatus = -1;
    }
  } else if (ErrorCode == ENOMEM || ErrorCode == EAGAIN) {
    // Fork failure.
    ProcessStatus = -1;
  } else {
    // Shell execution failure.
    ProcessStatus = W_EXITCODE(127, 0);
  }
  for (unsigned i = 0, n = sizeof(Argv) / sizeof(Argv[0]); i < n; ++i)
    free(Argv[i]);

  // Restore the signal handlers of the current process when the last thread
  // using this function finishes.
  {
    std::lock_guard<std::mutex> Lock(SignalMutex);
    --ActiveThreadCount;
    if (ActiveThreadCount == 0) {
      bool FailedRestore = false;
      if (sigaction(SIGINT, &OldSigIntAction, NULL) == -1) {
        Printf("Failed to restore SIGINT handling\n");
        FailedRestore = true;
      }
      if (sigaction(SIGQUIT, &OldSigQuitAction, NULL) == -1) {
        Printf("Failed to restore SIGQUIT handling\n");
        FailedRestore = true;
      }
      if (sigprocmask(SIG_BLOCK, &OldBlockedSignalsSet, NULL) == -1) {
        Printf("Failed to unblock SIGCHLD\n");
        FailedRestore = true;
      }
      if (FailedRestore)
        ProcessStatus = -1;
    }
  }
  return ProcessStatus;
}

void DiscardOutput(int Fd) {
  FILE* Temp = fopen("/dev/null", "w");
  if (!Temp)
    return;
  dup2(fileno(Temp), Fd);
  fclose(Temp);
}

void SetThreadName(std::thread &thread, const std::string &name) {
  // TODO ?
  // Darwin allows to set the name only on the current thread it seems
}

} // namespace fuzzer

#endif // LIBFUZZER_APPLE
