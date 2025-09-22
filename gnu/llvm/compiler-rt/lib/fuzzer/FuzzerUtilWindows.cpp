//===- FuzzerUtilWindows.cpp - Misc utils for Windows. --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Misc utils implementation for Windows.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"
#if LIBFUZZER_WINDOWS
#include "FuzzerCommand.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <io.h>
#include <iomanip>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
// clang-format off
#include <windows.h>
// These must be included after windows.h.
// archicture need to be set before including
// libloaderapi
#include <libloaderapi.h>
#include <stringapiset.h>
#include <psapi.h>
// clang-format on

namespace fuzzer {

static const FuzzingOptions* HandlerOpt = nullptr;

static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
  switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_STACK_OVERFLOW:
      if (HandlerOpt->HandleSegv)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_IN_PAGE_ERROR:
      if (HandlerOpt->HandleBus)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
      if (HandlerOpt->HandleIll)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
      if (HandlerOpt->HandleFpe)
        Fuzzer::StaticCrashSignalCallback();
      break;
    // This is an undocumented exception code corresponding to a Visual C++
    // Exception.
    //
    // See: https://devblogs.microsoft.com/oldnewthing/20100730-00/?p=13273
    case 0xE06D7363:
      if (HandlerOpt->HandleWinExcept)
        Fuzzer::StaticCrashSignalCallback();
      break;
      // TODO: Handle (Options.HandleXfsz)
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
  switch (dwCtrlType) {
    case CTRL_C_EVENT:
      if (HandlerOpt->HandleInt)
        Fuzzer::StaticInterruptCallback();
      return TRUE;
    case CTRL_BREAK_EVENT:
      if (HandlerOpt->HandleTerm)
        Fuzzer::StaticInterruptCallback();
      return TRUE;
  }
  return FALSE;
}

void CALLBACK AlarmHandler(PVOID, BOOLEAN) {
  Fuzzer::StaticAlarmCallback();
}

class TimerQ {
  HANDLE TimerQueue;
 public:
  TimerQ() : TimerQueue(NULL) {}
  ~TimerQ() {
    if (TimerQueue)
      DeleteTimerQueueEx(TimerQueue, NULL);
  }
  void SetTimer(int Seconds) {
    if (!TimerQueue) {
      TimerQueue = CreateTimerQueue();
      if (!TimerQueue) {
        Printf("libFuzzer: CreateTimerQueue failed.\n");
        exit(1);
      }
    }
    HANDLE Timer;
    if (!CreateTimerQueueTimer(&Timer, TimerQueue, AlarmHandler, NULL,
        Seconds*1000, Seconds*1000, 0)) {
      Printf("libFuzzer: CreateTimerQueueTimer failed.\n");
      exit(1);
    }
  }
};

static TimerQ Timer;

static void CrashHandler(int) { Fuzzer::StaticCrashSignalCallback(); }

void SetSignalHandler(const FuzzingOptions& Options) {
  HandlerOpt = &Options;

  if (Options.HandleAlrm && Options.UnitTimeoutSec > 0)
    Timer.SetTimer(Options.UnitTimeoutSec / 2 + 1);

  if (Options.HandleInt || Options.HandleTerm)
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
      DWORD LastError = GetLastError();
      Printf("libFuzzer: SetConsoleCtrlHandler failed (Error code: %lu).\n",
        LastError);
      exit(1);
    }

  if (Options.HandleSegv || Options.HandleBus || Options.HandleIll ||
      Options.HandleFpe || Options.HandleWinExcept)
    SetUnhandledExceptionFilter(ExceptionHandler);

  if (Options.HandleAbrt)
    if (SIG_ERR == signal(SIGABRT, CrashHandler)) {
      Printf("libFuzzer: signal failed with %d\n", errno);
      exit(1);
    }
}

void SleepSeconds(int Seconds) { Sleep(Seconds * 1000); }

unsigned long GetPid() { return GetCurrentProcessId(); }

size_t GetPeakRSSMb() {
  PROCESS_MEMORY_COUNTERS info;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
    return 0;
  return info.PeakWorkingSetSize >> 20;
}

FILE *OpenProcessPipe(const char *Command, const char *Mode) {
  return _popen(Command, Mode);
}

int CloseProcessPipe(FILE *F) {
  return _pclose(F);
}

int ExecuteCommand(const Command &Cmd) {
  std::string CmdLine = Cmd.toString();
  return system(CmdLine.c_str());
}

bool ExecuteCommand(const Command &Cmd, std::string *CmdOutput) {
  FILE *Pipe = _popen(Cmd.toString().c_str(), "r");
  if (!Pipe)
    return false;

  if (CmdOutput) {
    char TmpBuffer[128];
    while (fgets(TmpBuffer, sizeof(TmpBuffer), Pipe))
      CmdOutput->append(TmpBuffer);
  }
  return _pclose(Pipe) == 0;
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  // TODO: make this implementation more efficient.
  const char *Cdata = (const char *)Data;
  const char *Cpatt = (const char *)Patt;

  if (!Data || !Patt || DataLen == 0 || PattLen == 0 || DataLen < PattLen)
    return NULL;

  if (PattLen == 1)
    return memchr(Data, *Cpatt, DataLen);

  const char *End = Cdata + DataLen - PattLen + 1;

  for (const char *It = Cdata; It < End; ++It)
    if (It[0] == Cpatt[0] && memcmp(It, Cpatt, PattLen) == 0)
      return It;

  return NULL;
}

std::string DisassembleCmd(const std::string &FileName) {
  std::vector<std::string> command_vector;
  command_vector.push_back("dumpbin /summary > nul");
  if (ExecuteCommand(Command(command_vector)) == 0)
    return "dumpbin /disasm " + FileName;
  Printf("libFuzzer: couldn't find tool to disassemble (dumpbin)\n");
  exit(1);
}

std::string SearchRegexCmd(const std::string &Regex) {
  return "findstr /r \"" + Regex + "\"";
}

void DiscardOutput(int Fd) {
  FILE* Temp = fopen("nul", "w");
  if (!Temp)
    return;
  _dup2(_fileno(Temp), Fd);
  fclose(Temp);
}

size_t PageSize() {
  static size_t PageSizeCached = []() -> size_t {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
  }();
  return PageSizeCached;
}

void SetThreadName(std::thread &thread, const std::string &name) {
#ifndef __MINGW32__
  // Not setting the thread name in MinGW environments. MinGW C++ standard
  // libraries can either use native Windows threads or pthreads, so we
  // don't know with certainty what kind of thread handle we're getting
  // from thread.native_handle() here.
  typedef HRESULT(WINAPI * proc)(HANDLE, PCWSTR);
  HMODULE kbase = GetModuleHandleA("KernelBase.dll");
  proc ThreadNameProc =
      reinterpret_cast<proc>(GetProcAddress(kbase, "SetThreadDescription"));
  if (ThreadNameProc) {
    std::wstring buf;
    auto sz = MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, nullptr, 0);
    if (sz > 0) {
      buf.resize(sz);
      if (MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, &buf[0], sz) > 0) {
        (void)ThreadNameProc(thread.native_handle(), buf.c_str());
      }
    }
  }
#endif
}

} // namespace fuzzer

#endif // LIBFUZZER_WINDOWS
