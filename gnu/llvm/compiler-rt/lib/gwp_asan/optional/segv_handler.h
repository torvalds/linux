//===-- segv_handler.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTIONAL_SEGV_HANDLER_H_
#define GWP_ASAN_OPTIONAL_SEGV_HANDLER_H_

#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/optional/backtrace.h"
#include "gwp_asan/optional/printf.h"

namespace gwp_asan {
namespace segv_handler {
// Install the SIGSEGV crash handler for printing use-after-free and heap-
// buffer-{under|over}flow exceptions if the user asked for it. This is platform
// specific as even though POSIX and Windows both support registering handlers
// through signal(), we have to use platform-specific signal handlers to obtain
// the address that caused the SIGSEGV exception. GPA->init() must be called
// before this function.
void installSignalHandlers(gwp_asan::GuardedPoolAllocator *GPA, Printf_t Printf,
                           gwp_asan::backtrace::PrintBacktrace_t PrintBacktrace,
                           gwp_asan::backtrace::SegvBacktrace_t SegvBacktrace,
                           bool Recoverable = false);

// Uninistall the signal handlers, test-only.
void uninstallSignalHandlers();
} // namespace segv_handler
} // namespace gwp_asan

#endif // GWP_ASAN_OPTIONAL_SEGV_HANDLER_H_
