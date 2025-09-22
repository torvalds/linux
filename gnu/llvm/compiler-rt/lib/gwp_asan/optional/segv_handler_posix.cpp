//===-- segv_handler_posix.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/common.h"
#include "gwp_asan/crash_handler.h"
#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/optional/segv_handler.h"
#include "gwp_asan/options.h"

// RHEL creates the PRIu64 format macro (for printing uint64_t's) only when this
// macro is defined before including <inttypes.h>.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif

#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>

using gwp_asan::AllocationMetadata;
using gwp_asan::Error;
using gwp_asan::GuardedPoolAllocator;
using gwp_asan::Printf_t;
using gwp_asan::backtrace::PrintBacktrace_t;
using gwp_asan::backtrace::SegvBacktrace_t;

namespace {

struct ScopedEndOfReportDecorator {
  ScopedEndOfReportDecorator(gwp_asan::Printf_t Printf) : Printf(Printf) {}
  ~ScopedEndOfReportDecorator() { Printf("*** End GWP-ASan report ***\n"); }
  gwp_asan::Printf_t Printf;
};

// Prints the provided error and metadata information.
void printHeader(Error E, uintptr_t AccessPtr,
                 const gwp_asan::AllocationMetadata *Metadata,
                 Printf_t Printf) {
  // Print using intermediate strings. Platforms like Android don't like when
  // you print multiple times to the same line, as there may be a newline
  // appended to a log file automatically per Printf() call.
  constexpr size_t kDescriptionBufferLen = 128;
  char DescriptionBuffer[kDescriptionBufferLen] = "";

  bool AccessWasInBounds = false;
  if (E != Error::UNKNOWN && Metadata != nullptr) {
    uintptr_t Address = __gwp_asan_get_allocation_address(Metadata);
    size_t Size = __gwp_asan_get_allocation_size(Metadata);
    if (AccessPtr < Address) {
      snprintf(DescriptionBuffer, kDescriptionBufferLen,
               "(%zu byte%s to the left of a %zu-byte allocation at 0x%zx) ",
               Address - AccessPtr, (Address - AccessPtr == 1) ? "" : "s", Size,
               Address);
    } else if (AccessPtr > Address) {
      snprintf(DescriptionBuffer, kDescriptionBufferLen,
               "(%zu byte%s to the right of a %zu-byte allocation at 0x%zx) ",
               AccessPtr - Address, (AccessPtr - Address == 1) ? "" : "s", Size,
               Address);
    } else if (E == Error::DOUBLE_FREE) {
      snprintf(DescriptionBuffer, kDescriptionBufferLen,
               "(a %zu-byte allocation) ", Size);
    } else {
      AccessWasInBounds = true;
      snprintf(DescriptionBuffer, kDescriptionBufferLen,
               "(%zu byte%s into a %zu-byte allocation at 0x%zx) ",
               AccessPtr - Address, (AccessPtr - Address == 1) ? "" : "s", Size,
               Address);
    }
  }

  // Possible number of digits of a 64-bit number: ceil(log10(2^64)) == 20. Add
  // a null terminator, and round to the nearest 8-byte boundary.
  uint64_t ThreadID = gwp_asan::getThreadID();
  constexpr size_t kThreadBufferLen = 24;
  char ThreadBuffer[kThreadBufferLen];
  if (ThreadID == gwp_asan::kInvalidThreadID)
    snprintf(ThreadBuffer, kThreadBufferLen, "<unknown>");
  else
    snprintf(ThreadBuffer, kThreadBufferLen, "%" PRIu64, ThreadID);

  const char *OutOfBoundsAndUseAfterFreeWarning = "";
  if (E == Error::USE_AFTER_FREE && !AccessWasInBounds) {
    OutOfBoundsAndUseAfterFreeWarning =
        " (warning: buffer overflow/underflow detected on a free()'d "
        "allocation. This either means you have a buffer-overflow and a "
        "use-after-free at the same time, or you have a long-lived "
        "use-after-free bug where the allocation/deallocation metadata below "
        "has already been overwritten and is likely bogus)";
  }

  Printf("%s%s at 0x%zx %sby thread %s here:\n", gwp_asan::ErrorToString(E),
         OutOfBoundsAndUseAfterFreeWarning, AccessPtr, DescriptionBuffer,
         ThreadBuffer);
}

static bool HasReportedBadPoolAccess = false;
static const char *kUnknownCrashText =
    "GWP-ASan cannot provide any more information about this error. This may "
    "occur due to a wild memory access into the GWP-ASan pool, or an "
    "overflow/underflow that is > 512B in length.\n";

void dumpReport(uintptr_t ErrorPtr, const gwp_asan::AllocatorState *State,
                const gwp_asan::AllocationMetadata *Metadata,
                SegvBacktrace_t SegvBacktrace, Printf_t Printf,
                PrintBacktrace_t PrintBacktrace, void *Context) {
  assert(State && "dumpReport missing Allocator State.");
  assert(Metadata && "dumpReport missing Metadata.");
  assert(Printf && "dumpReport missing Printf.");
  assert(__gwp_asan_error_is_mine(State, ErrorPtr) &&
         "dumpReport() called on a non-GWP-ASan error.");

  uintptr_t InternalErrorPtr =
      __gwp_asan_get_internal_crash_address(State, ErrorPtr);
  if (InternalErrorPtr)
    ErrorPtr = InternalErrorPtr;

  const gwp_asan::AllocationMetadata *AllocMeta =
      __gwp_asan_get_metadata(State, Metadata, ErrorPtr);

  if (AllocMeta == nullptr) {
    if (HasReportedBadPoolAccess) return;
    HasReportedBadPoolAccess = true;
    Printf("*** GWP-ASan detected a memory error ***\n");
    ScopedEndOfReportDecorator Decorator(Printf);
    Printf(kUnknownCrashText);
    return;
  }

  // It's unusual for a signal handler to be invoked multiple times for the same
  // allocation, but it's possible in various scenarios, like:
  //  1. A double-free or invalid-free was invoked in one thread at the same
  //     time as a buffer-overflow or use-after-free in another thread, or
  //  2. Two threads do a use-after-free or buffer-overflow at the same time.
  // In these instances, we've already dumped a report for this allocation, so
  // skip dumping this issue as well.
  if (AllocMeta->HasCrashed)
    return;

  Printf("*** GWP-ASan detected a memory error ***\n");
  ScopedEndOfReportDecorator Decorator(Printf);

  Error E = __gwp_asan_diagnose_error(State, Metadata, ErrorPtr);
  if (E == Error::UNKNOWN) {
    Printf(kUnknownCrashText);
    return;
  }

  // Print the error header.
  printHeader(E, ErrorPtr, AllocMeta, Printf);

  // Print the fault backtrace.
  static constexpr unsigned kMaximumStackFramesForCrashTrace = 512;
  uintptr_t Trace[kMaximumStackFramesForCrashTrace];
  size_t TraceLength =
      SegvBacktrace(Trace, kMaximumStackFramesForCrashTrace, Context);

  PrintBacktrace(Trace, TraceLength, Printf);

  // Maybe print the deallocation trace.
  if (__gwp_asan_is_deallocated(AllocMeta)) {
    uint64_t ThreadID = __gwp_asan_get_deallocation_thread_id(AllocMeta);
    if (ThreadID == gwp_asan::kInvalidThreadID)
      Printf("0x%zx was deallocated by thread <unknown> here:\n", ErrorPtr);
    else
      Printf("0x%zx was deallocated by thread %zu here:\n", ErrorPtr, ThreadID);
    TraceLength = __gwp_asan_get_deallocation_trace(
        AllocMeta, Trace, kMaximumStackFramesForCrashTrace);
    PrintBacktrace(Trace, TraceLength, Printf);
  }

  // Print the allocation trace.
  uint64_t ThreadID = __gwp_asan_get_allocation_thread_id(AllocMeta);
  if (ThreadID == gwp_asan::kInvalidThreadID)
    Printf("0x%zx was allocated by thread <unknown> here:\n", ErrorPtr);
  else
    Printf("0x%zx was allocated by thread %zu here:\n", ErrorPtr, ThreadID);
  TraceLength = __gwp_asan_get_allocation_trace(
      AllocMeta, Trace, kMaximumStackFramesForCrashTrace);
  PrintBacktrace(Trace, TraceLength, Printf);
}

struct sigaction PreviousHandler;
bool SignalHandlerInstalled;
bool RecoverableSignal;
gwp_asan::GuardedPoolAllocator *GPAForSignalHandler;
Printf_t PrintfForSignalHandler;
PrintBacktrace_t PrintBacktraceForSignalHandler;
SegvBacktrace_t BacktraceForSignalHandler;

static void sigSegvHandler(int sig, siginfo_t *info, void *ucontext) {
  const gwp_asan::AllocatorState *State =
      GPAForSignalHandler->getAllocatorState();
  void *FaultAddr = info->si_addr;
  uintptr_t FaultAddrUPtr = reinterpret_cast<uintptr_t>(FaultAddr);

  if (__gwp_asan_error_is_mine(State, FaultAddrUPtr)) {
    GPAForSignalHandler->preCrashReport(FaultAddr);

    dumpReport(FaultAddrUPtr, State, GPAForSignalHandler->getMetadataRegion(),
               BacktraceForSignalHandler, PrintfForSignalHandler,
               PrintBacktraceForSignalHandler, ucontext);

    if (RecoverableSignal) {
      GPAForSignalHandler->postCrashReportRecoverableOnly(FaultAddr);
      return;
    }
  }

  // Process any previous handlers as long as the crash wasn't a GWP-ASan crash
  // in recoverable mode.
  if (PreviousHandler.sa_flags & SA_SIGINFO) {
    PreviousHandler.sa_sigaction(sig, info, ucontext);
  } else if (PreviousHandler.sa_handler == SIG_DFL) {
    // If the previous handler was the default handler, cause a core dump.
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
  } else if (PreviousHandler.sa_handler == SIG_IGN) {
    // If the previous segv handler was SIGIGN, crash iff we were responsible
    // for the crash.
    if (__gwp_asan_error_is_mine(GPAForSignalHandler->getAllocatorState(),
                                 reinterpret_cast<uintptr_t>(info->si_addr))) {
      signal(SIGSEGV, SIG_DFL);
      raise(SIGSEGV);
    }
  } else {
    PreviousHandler.sa_handler(sig);
  }
}
} // anonymous namespace

namespace gwp_asan {
namespace segv_handler {

void installSignalHandlers(gwp_asan::GuardedPoolAllocator *GPA, Printf_t Printf,
                           PrintBacktrace_t PrintBacktrace,
                           SegvBacktrace_t SegvBacktrace, bool Recoverable) {
  assert(GPA && "GPA wasn't provided to installSignalHandlers.");
  assert(Printf && "Printf wasn't provided to installSignalHandlers.");
  assert(PrintBacktrace &&
         "PrintBacktrace wasn't provided to installSignalHandlers.");
  assert(SegvBacktrace &&
         "SegvBacktrace wasn't provided to installSignalHandlers.");
  GPAForSignalHandler = GPA;
  PrintfForSignalHandler = Printf;
  PrintBacktraceForSignalHandler = PrintBacktrace;
  BacktraceForSignalHandler = SegvBacktrace;
  RecoverableSignal = Recoverable;

  struct sigaction Action = {};
  Action.sa_sigaction = sigSegvHandler;
  Action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &Action, &PreviousHandler);
  SignalHandlerInstalled = true;
  HasReportedBadPoolAccess = false;
}

void uninstallSignalHandlers() {
  if (SignalHandlerInstalled) {
    sigaction(SIGSEGV, &PreviousHandler, nullptr);
    SignalHandlerInstalled = false;
  }
}
} // namespace segv_handler
} // namespace gwp_asan
