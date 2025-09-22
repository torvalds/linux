//===-- segv_handler_fuchsia.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/optional/segv_handler.h"

// GWP-ASan on Fuchsia doesn't currently support signal handlers.

namespace gwp_asan {
namespace segv_handler {
void installSignalHandlers(gwp_asan::GuardedPoolAllocator * /* GPA */,
                           Printf_t /* Printf */,
                           backtrace::PrintBacktrace_t /* PrintBacktrace */,
                           backtrace::SegvBacktrace_t /* SegvBacktrace */,
                           bool /* Recoverable */) {}

void uninstallSignalHandlers() {}
} // namespace segv_handler
} // namespace gwp_asan
