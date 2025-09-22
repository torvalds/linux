//===-- backtrace.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTIONAL_BACKTRACE_H_
#define GWP_ASAN_OPTIONAL_BACKTRACE_H_

#include "gwp_asan/optional/printf.h"
#include "gwp_asan/options.h"

namespace gwp_asan {
namespace backtrace {
// ================================ Description ================================
// This function shall take the backtrace provided in `TraceBuffer`, and print
// it in a human-readable format using `Print`. Generally, this function shall
// resolve raw pointers to section offsets and print them with the following
// sanitizer-common format:
//      "  #{frame_number} {pointer} in {function name} ({binary name}+{offset}"
// e.g. "  #5 0x420459 in _start (/tmp/uaf+0x420459)"
// This format allows the backtrace to be symbolized offline successfully using
// llvm-symbolizer.
// =================================== Notes ===================================
// This function may directly or indirectly call malloc(), as the
// GuardedPoolAllocator contains a reentrancy barrier to prevent infinite
// recursion. Any allocation made inside this function will be served by the
// supporting allocator, and will not have GWP-ASan protections.
typedef void (*PrintBacktrace_t)(uintptr_t *TraceBuffer, size_t TraceLength,
                                 Printf_t Print);

// Returns a function pointer to a backtrace function that's suitable for
// unwinding through a signal handler. This is important primarily for frame-
// pointer based unwinders, DWARF or other unwinders can simply provide the
// normal backtrace function as the implementation here. On POSIX, SignalContext
// should be the `ucontext_t` from the signal handler.
typedef size_t (*SegvBacktrace_t)(uintptr_t *TraceBuffer, size_t Size,
                                  void *SignalContext);

// Returns platform-specific provided implementations of Backtrace_t for use
// inside the GWP-ASan core allocator.
options::Backtrace_t getBacktraceFunction();

// Returns platform-specific provided implementations of PrintBacktrace_t and
// SegvBacktrace_t for use in the optional SEGV handler.
PrintBacktrace_t getPrintBacktraceFunction();
SegvBacktrace_t getSegvBacktraceFunction();
} // namespace backtrace
} // namespace gwp_asan

#endif // GWP_ASAN_OPTIONAL_BACKTRACE_H_
