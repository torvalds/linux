//===-- asan_stack.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Code for ASan stack trace.
//===----------------------------------------------------------------------===//
#include "asan_internal.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_atomic.h"

namespace __asan {

static atomic_uint32_t malloc_context_size;

void SetMallocContextSize(u32 size) {
  atomic_store(&malloc_context_size, size, memory_order_release);
}

u32 GetMallocContextSize() {
  return atomic_load(&malloc_context_size, memory_order_acquire);
}

namespace {

// ScopedUnwinding is a scope for stacktracing member of a context
class ScopedUnwinding {
 public:
  explicit ScopedUnwinding(AsanThread *t) : thread(t) {
    if (thread) {
      can_unwind = !thread->isUnwinding();
      thread->setUnwinding(true);
    }
  }
  ~ScopedUnwinding() {
    if (thread)
      thread->setUnwinding(false);
  }

  bool CanUnwind() const { return can_unwind; }

 private:
  AsanThread *thread = nullptr;
  bool can_unwind = true;
};

}  // namespace

}  // namespace __asan

void __sanitizer::BufferedStackTrace::UnwindImpl(
    uptr pc, uptr bp, void *context, bool request_fast, u32 max_depth) {
  using namespace __asan;
  size = 0;
  if (UNLIKELY(!AsanInited()))
    return;
  request_fast = StackTrace::WillUseFastUnwind(request_fast);
  AsanThread *t = GetCurrentThread();
  ScopedUnwinding unwind_scope(t);
  if (!unwind_scope.CanUnwind())
    return;
  if (request_fast) {
    if (t) {
      Unwind(max_depth, pc, bp, nullptr, t->stack_top(), t->stack_bottom(),
             true);
    }
    return;
  }
  if (SANITIZER_MIPS && t &&
      !IsValidFrame(bp, t->stack_top(), t->stack_bottom()))
    return;
  Unwind(max_depth, pc, bp, context, t ? t->stack_top() : 0,
         t ? t->stack_bottom() : 0, false);
}

// ------------------ Interface -------------- {{{1

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  using namespace __asan;
  PRINT_CURRENT_STACK();
}
}  // extern "C"
