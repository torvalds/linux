//===-- backtrace_sanitizer_common.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gwp_asan/optional/backtrace.h"
#include "gwp_asan/options.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

void __sanitizer::BufferedStackTrace::UnwindImpl(uptr pc, uptr bp,
                                                 void *context,
                                                 bool request_fast,
                                                 u32 max_depth) {
  if (!StackTrace::WillUseFastUnwind(request_fast))
    return Unwind(max_depth, pc, 0, context, 0, 0, false);

  uptr top = 0;
  uptr bottom = 0;
  GetThreadStackTopAndBottom(/*at_initialization*/ false, &top, &bottom);

  return Unwind(max_depth, pc, bp, context, top, bottom, request_fast);
}

namespace {
size_t BacktraceCommon(uintptr_t *TraceBuffer, size_t Size, void *Context) {
  // Use the slow sanitizer unwinder in the segv handler. Fast frame pointer
  // unwinders can end up dropping frames because the kernel sigreturn() frame's
  // return address is the return address at time of fault. This has the result
  // of never actually capturing the PC where the signal was raised.
  bool UseFastUnwind = (Context == nullptr);

  __sanitizer::BufferedStackTrace Trace;
  Trace.Reset();
  if (Size > __sanitizer::kStackTraceMax)
    Size = __sanitizer::kStackTraceMax;

  Trace.Unwind((__sanitizer::uptr)__builtin_return_address(0),
               (__sanitizer::uptr)__builtin_frame_address(0), Context,
               UseFastUnwind, Size - 1);

  memcpy(TraceBuffer, Trace.trace, Trace.size * sizeof(uintptr_t));
  return Trace.size;
}

size_t Backtrace(uintptr_t *TraceBuffer, size_t Size) {
  return BacktraceCommon(TraceBuffer, Size, nullptr);
}

size_t SegvBacktrace(uintptr_t *TraceBuffer, size_t Size, void *Context) {
  return BacktraceCommon(TraceBuffer, Size, Context);
}

static void PrintBacktrace(uintptr_t *Trace, size_t TraceLength,
                           gwp_asan::Printf_t Printf) {
  __sanitizer::StackTrace StackTrace;
  StackTrace.trace = reinterpret_cast<__sanitizer::uptr *>(Trace);
  StackTrace.size = TraceLength;

  if (StackTrace.size == 0) {
    Printf("  <unknown (does your allocator support backtracing?)>\n\n");
    return;
  }

  __sanitizer::InternalScopedString buffer;
  StackTrace.PrintTo(&buffer);
  Printf("%s\n", buffer.data());
}
} // anonymous namespace

namespace gwp_asan {
namespace backtrace {

// This function is thread-compatible. It must be synchronised in respect to any
// other calls to getBacktraceFunction(), calls to getPrintBacktraceFunction(),
// and calls to either of the functions that they return. Furthermore, this may
// require synchronisation with any calls to sanitizer_common that use flags.
// Generally, this function will be called during the initialisation of the
// allocator, which is done in a thread-compatible manner.
options::Backtrace_t getBacktraceFunction() {
  // The unwinder requires the default flags to be set.
  __sanitizer::SetCommonFlagsDefaults();
  __sanitizer::InitializeCommonFlags();
  return Backtrace;
}

PrintBacktrace_t getPrintBacktraceFunction() { return PrintBacktrace; }
SegvBacktrace_t getSegvBacktraceFunction() { return SegvBacktrace; }

} // namespace backtrace
} // namespace gwp_asan
