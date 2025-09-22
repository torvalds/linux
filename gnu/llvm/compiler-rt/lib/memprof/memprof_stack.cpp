//===-- memprof_stack.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Code for MemProf stack trace.
//===----------------------------------------------------------------------===//
#include "memprof_stack.h"
#include "memprof_internal.h"
#include "sanitizer_common/sanitizer_atomic.h"

namespace __memprof {

static atomic_uint32_t malloc_context_size;

void SetMallocContextSize(u32 size) {
  atomic_store(&malloc_context_size, size, memory_order_release);
}

u32 GetMallocContextSize() {
  return atomic_load(&malloc_context_size, memory_order_acquire);
}

} // namespace __memprof

void __sanitizer::BufferedStackTrace::UnwindImpl(uptr pc, uptr bp,
                                                 void *context,
                                                 bool request_fast,
                                                 u32 max_depth) {
  using namespace __memprof;
  size = 0;
  if (UNLIKELY(!memprof_inited))
    return;
  request_fast = StackTrace::WillUseFastUnwind(request_fast);
  MemprofThread *t = GetCurrentThread();
  if (request_fast) {
    if (t) {
      Unwind(max_depth, pc, bp, nullptr, t->stack_top(), t->stack_bottom(),
             true);
    }
    return;
  }
  Unwind(max_depth, pc, bp, context, 0, 0, false);
}

// ------------------ Interface -------------- {{{1

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  using namespace __memprof;
  PRINT_CURRENT_STACK();
}
} // extern "C"
