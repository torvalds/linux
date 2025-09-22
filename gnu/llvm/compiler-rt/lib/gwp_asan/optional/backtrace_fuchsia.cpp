//===-- backtrace_fuchsia.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/optional/backtrace.h"

#include <zircon/sanitizer.h>

namespace gwp_asan {
namespace backtrace {

// Fuchsia's C library provides safe, fast, best-effort backtraces itself.
options::Backtrace_t getBacktraceFunction() {
  return __sanitizer_fast_backtrace;
}

// These are only used in fatal signal handling, which is not used on Fuchsia.

PrintBacktrace_t getPrintBacktraceFunction() { return nullptr; }
SegvBacktrace_t getSegvBacktraceFunction() { return nullptr; }

} // namespace backtrace
} // namespace gwp_asan
