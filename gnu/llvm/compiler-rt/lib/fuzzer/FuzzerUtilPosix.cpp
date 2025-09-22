//===- FuzzerUtilPosix.cpp - Misc utils for Posix. ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Misc utils implementation using Posix API.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"
#if LIBFUZZER_POSIX
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include "FuzzerTracePC.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace fuzzer {

static void AlarmHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticAlarmCallback();
}

static void (*upstream_segv_handler)(int, siginfo_t *, void *);

static void SegvHandler(int sig, siginfo_t *si, void *ucontext) {
  assert(si->si_signo == SIGSEGV);
  if (upstream_segv_handler)
    return upstream_segv_handler(sig, si, ucontext);
  Fuzzer::StaticCrashSignalCallback();
}

static void CrashHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticCrashSignalCallback();
}

static void InterruptHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticInterruptCallback();
}

static void GracefulExitHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticGracefulExitCallback();
}

static void FileSizeExceedHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticFileSizeExceedCallback();
}

static void SetSigaction(int signum,
                         void (*callback)(int, siginfo_t *, void *)) {
  struct sigaction sigact = {};
  if (sigaction(signum, nullptr, &sigact)) {
    Printf("libFuzzer: sigaction failed with %d\n", errno);
    exit(1);
  }
  if (sigact.sa_flags & SA_SIGINFO) {
    if (sigact.sa_sigaction) {
      if (signum != SIGSEGV)
        return;
      upstream_segv_handler = sigact.sa_sigaction;
    }
  } else {
    if (sigact.sa_handler != SIG_DFL && sigact.sa_handler != SIG_IGN &&
        sigact.sa_handler != SIG_ERR)
      return;
  }

  struct sigaction new_sigact = {};
  // Address sanitizer needs SA_ONSTACK (causing the signal handler to run on a
  // dedicated stack) in order to be able to detect stack overflows; keep the
  // flag if it's set.
  new_sigact.sa_flags = SA_SIGINFO | (sigact.sa_flags & SA_ONSTACK);
  new_sigact.sa_sigaction = callback;
  if (sigaction(signum, &new_sigact, nullptr)) {
    Printf("libFuzzer: sigaction failed with %d\n", errno);
    exit(1);
  }
}

// Return true on success, false otherwise.
bool ExecuteCommand(const Command &Cmd, std::string *CmdOutput) {
  FILE *Pipe = popen(Cmd.toString().c_str(), "r");
  if (!Pipe)
    return false;

  if (CmdOutput) {
    char TmpBuffer[128];
    while (fgets(TmpBuffer, sizeof(TmpBuffer), Pipe))
      CmdOutput->append(TmpBuffer);
  }
  return pclose(Pipe) == 0;
}

void SetTimer(int Seconds) {
  struct itimerval T {
    {Seconds, 0}, { Seconds, 0 }
  };
  if (setitimer(ITIMER_REAL, &T, nullptr)) {
    Printf("libFuzzer: setitimer failed with %d\n", errno);
    exit(1);
  }
  SetSigaction(SIGALRM, AlarmHandler);
}

void SetSignalHandler(const FuzzingOptions& Options) {
  // setitimer is not implemented in emscripten.
  if (Options.HandleAlrm && Options.UnitTimeoutSec > 0 && !LIBFUZZER_EMSCRIPTEN)
    SetTimer(Options.UnitTimeoutSec / 2 + 1);
  if (Options.HandleInt)
    SetSigaction(SIGINT, InterruptHandler);
  if (Options.HandleTerm)
    SetSigaction(SIGTERM, InterruptHandler);
  if (Options.HandleSegv)
    SetSigaction(SIGSEGV, SegvHandler);
  if (Options.HandleBus)
    SetSigaction(SIGBUS, CrashHandler);
  if (Options.HandleAbrt)
    SetSigaction(SIGABRT, CrashHandler);
  if (Options.HandleIll)
    SetSigaction(SIGILL, CrashHandler);
  if (Options.HandleFpe)
    SetSigaction(SIGFPE, CrashHandler);
  if (Options.HandleXfsz)
    SetSigaction(SIGXFSZ, FileSizeExceedHandler);
  if (Options.HandleUsr1)
    SetSigaction(SIGUSR1, GracefulExitHandler);
  if (Options.HandleUsr2)
    SetSigaction(SIGUSR2, GracefulExitHandler);
}

void SleepSeconds(int Seconds) {
  sleep(Seconds); // Use C API to avoid coverage from instrumented libc++.
}

unsigned long GetPid() { return (unsigned long)getpid(); }

size_t GetPeakRSSMb() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0;
  if (LIBFUZZER_LINUX || LIBFUZZER_FREEBSD || LIBFUZZER_NETBSD ||
      LIBFUZZER_EMSCRIPTEN) {
    // ru_maxrss is in KiB
    return usage.ru_maxrss >> 10;
  } else if (LIBFUZZER_APPLE) {
    // ru_maxrss is in bytes
    return usage.ru_maxrss >> 20;
  }
  assert(0 && "GetPeakRSSMb() is not implemented for your platform");
  return 0;
}

FILE *OpenProcessPipe(const char *Command, const char *Mode) {
  return popen(Command, Mode);
}

int CloseProcessPipe(FILE *F) {
  return pclose(F);
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  return memmem(Data, DataLen, Patt, PattLen);
}

std::string DisassembleCmd(const std::string &FileName) {
  return "objdump -d " + FileName;
}

std::string SearchRegexCmd(const std::string &Regex) {
  return "grep '" + Regex + "'";
}

size_t PageSize() {
  static size_t PageSizeCached = sysconf(_SC_PAGESIZE);
  return PageSizeCached;
}

}  // namespace fuzzer

#endif // LIBFUZZER_POSIX
