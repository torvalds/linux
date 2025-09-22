//===-- memprof_stack.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf-private header for memprof_stack.cpp.
//===----------------------------------------------------------------------===//

#ifndef MEMPROF_STACK_H
#define MEMPROF_STACK_H

#include "memprof_flags.h"
#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

namespace __memprof {

static const u32 kDefaultMallocContextSize = 30;

void SetMallocContextSize(u32 size);
u32 GetMallocContextSize();

} // namespace __memprof

// NOTE: A Rule of thumb is to retrieve stack trace in the interceptors
// as early as possible (in functions exposed to the user), as we generally
// don't want stack trace to contain functions from MemProf internals.

#define GET_STACK_TRACE(max_size, fast)                                        \
  BufferedStackTrace stack;                                                    \
  if (max_size <= 2) {                                                         \
    stack.size = max_size;                                                     \
    if (max_size > 0) {                                                        \
      stack.top_frame_bp = GET_CURRENT_FRAME();                                \
      stack.trace_buffer[0] = StackTrace::GetCurrentPc();                      \
      if (max_size > 1)                                                        \
        stack.trace_buffer[1] = GET_CALLER_PC();                               \
    }                                                                          \
  } else {                                                                     \
    stack.Unwind(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(), nullptr,     \
                 fast, max_size);                                              \
  }

#define GET_STACK_TRACE_FATAL_HERE                                             \
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_fatal)

#define GET_STACK_TRACE_THREAD GET_STACK_TRACE(kStackTraceMax, true)

#define GET_STACK_TRACE_MALLOC                                                 \
  GET_STACK_TRACE(GetMallocContextSize(), common_flags()->fast_unwind_on_malloc)

#define GET_STACK_TRACE_FREE GET_STACK_TRACE_MALLOC

#define PRINT_CURRENT_STACK()                                                  \
  {                                                                            \
    GET_STACK_TRACE_FATAL_HERE;                                                \
    stack.Print();                                                             \
  }

#endif // MEMPROF_STACK_H
