//===-- sanitizer_stacktrace_sparc.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//
// Implementation of fast stack unwinding for Sparc.
//===----------------------------------------------------------------------===//

#if defined(__sparc__)

#if defined(__arch64__) || defined(__sparcv9)
#define STACK_BIAS 2047
#else
#define STACK_BIAS 0
#endif

#include "sanitizer_common.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

void BufferedStackTrace::UnwindFast(uptr pc, uptr bp, uptr stack_top,
                                    uptr stack_bottom, u32 max_depth) {
  // TODO(yln): add arg sanity check for stack_top/stack_bottom
  CHECK_GE(max_depth, 2);
  const uptr kPageSize = GetPageSizeCached();
  trace_buffer[0] = pc;
  size = 1;
  if (stack_top < 4096) return;  // Sanity check for stack top.
  // Flush register windows to memory
#if defined(__sparc_v9__) || defined(__sparcv9__) || defined(__sparcv9)
  asm volatile("flushw" ::: "memory");
#else
  asm volatile("ta 3" ::: "memory");
#endif
  // On the SPARC, the return address is not in the frame, it is in a
  // register.  There is no way to access it off of the current frame
  // pointer, but it can be accessed off the previous frame pointer by
  // reading the value from the register window save area.
  uptr prev_bp = GET_CURRENT_FRAME();
  uptr next_bp = prev_bp;
  unsigned int i = 0;
  while (next_bp != bp && IsAligned(next_bp, sizeof(uhwptr)) && i++ < 8) {
    prev_bp = next_bp;
    next_bp = (uptr)((uhwptr *)next_bp)[14] + STACK_BIAS;
  }
  if (next_bp == bp)
    bp = prev_bp;
  // Lowest possible address that makes sense as the next frame pointer.
  // Goes up as we walk the stack.
  uptr bottom = stack_bottom;
  // Avoid infinite loop when frame == frame[0] by using frame > prev_frame.
  while (IsValidFrame(bp, stack_top, bottom) && IsAligned(bp, sizeof(uhwptr)) &&
         size < max_depth) {
    // %o7 contains the address of the call instruction and not the
    // return address, so we need to compensate.
    uhwptr pc1 = GetNextInstructionPc(((uhwptr *)bp)[15]);
    // Let's assume that any pointer in the 0th page is invalid and
    // stop unwinding here.  If we're adding support for a platform
    // where this isn't true, we need to reconsider this check.
    if (pc1 < kPageSize)
      break;
    if (pc1 != pc)
      trace_buffer[size++] = pc1;
    bottom = bp;
    bp = (uptr)((uhwptr *)bp)[14] + STACK_BIAS;
  }
}

}  // namespace __sanitizer

#endif  // !defined(__sparc__)
