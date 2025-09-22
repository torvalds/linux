//===------------------ sanitizer_unwind_fuchsia.cpp
//---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// Sanitizer unwind Fuchsia specific functions.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_FUCHSIA

#  include <limits.h>
#  include <unwind.h>

#  include "sanitizer_common.h"
#  include "sanitizer_stacktrace.h"

namespace __sanitizer {

#  if SANITIZER_CAN_SLOW_UNWIND
struct UnwindTraceArg {
  BufferedStackTrace *stack;
  u32 max_depth;
};

_Unwind_Reason_Code Unwind_Trace(struct _Unwind_Context *ctx, void *param) {
  UnwindTraceArg *arg = static_cast<UnwindTraceArg *>(param);
  CHECK_LT(arg->stack->size, arg->max_depth);
  uptr pc = _Unwind_GetIP(ctx);
  if (pc < GetPageSizeCached())
    return _URC_NORMAL_STOP;
  arg->stack->trace_buffer[arg->stack->size++] = pc;
  return (arg->stack->size == arg->max_depth ? _URC_NORMAL_STOP
                                             : _URC_NO_REASON);
}

void BufferedStackTrace::UnwindSlow(uptr pc, u32 max_depth) {
  CHECK_GE(max_depth, 2);
  size = 0;
  UnwindTraceArg arg = {this, Min(max_depth + 1, kStackTraceMax)};
  _Unwind_Backtrace(Unwind_Trace, &arg);
  CHECK_GT(size, 0);
  // We need to pop a few frames so that pc is on top.
  uptr to_pop = LocatePcInTrace(pc);
  // trace_buffer[0] belongs to the current function so we always pop it,
  // unless there is only 1 frame in the stack trace (1 frame is always better
  // than 0!).
  PopStackFrames(Min(to_pop, static_cast<uptr>(1)));
  trace_buffer[0] = pc;
}

void BufferedStackTrace::UnwindSlow(uptr pc, void *context, u32 max_depth) {
  CHECK(context);
  CHECK_GE(max_depth, 2);
  UNREACHABLE("signal context doesn't exist");
}
#  endif  //  SANITIZER_CAN_SLOW_UNWIND

}  // namespace __sanitizer

#endif  // SANITIZER_FUCHSIA
