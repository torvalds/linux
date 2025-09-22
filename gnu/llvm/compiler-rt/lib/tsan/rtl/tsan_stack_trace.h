//===-- tsan_stack_trace.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_STACK_TRACE_H
#define TSAN_STACK_TRACE_H

#include "sanitizer_common/sanitizer_stacktrace.h"
#include "tsan_defs.h"

namespace __tsan {

// StackTrace which calls malloc/free to allocate the buffer for
// addresses in stack traces.
struct VarSizeStackTrace : public StackTrace {
  uptr *trace_buffer;  // Owned.

  VarSizeStackTrace();
  ~VarSizeStackTrace();
  void Init(const uptr *pcs, uptr cnt, uptr extra_top_pc = 0);

  // Reverses the current stack trace order, the top frame goes to the bottom,
  // the last frame goes to the top.
  void ReverseOrder();

 private:
  void ResizeBuffer(uptr new_size);

  VarSizeStackTrace(const VarSizeStackTrace &);
  void operator=(const VarSizeStackTrace &);
};

}  // namespace __tsan

#endif  // TSAN_STACK_TRACE_H
