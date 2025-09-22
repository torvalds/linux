//===-- sanitizer_stacktrace.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_STACKTRACE_H
#define SANITIZER_STACKTRACE_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

namespace __sanitizer {

struct BufferedStackTrace;

static const u32 kStackTraceMax = 255;

#if SANITIZER_LINUX && defined(__mips__)
# define SANITIZER_CAN_FAST_UNWIND 0
#elif SANITIZER_WINDOWS
# define SANITIZER_CAN_FAST_UNWIND 0
#else
# define SANITIZER_CAN_FAST_UNWIND 1
#endif

// Fast unwind is the only option on Mac for now; we will need to
// revisit this macro when slow unwind works on Mac, see
// https://github.com/google/sanitizers/issues/137
#if SANITIZER_APPLE
#  define SANITIZER_CAN_SLOW_UNWIND 0
#else
# define SANITIZER_CAN_SLOW_UNWIND 1
#endif

struct StackTrace {
  const uptr *trace;
  u32 size;
  u32 tag;

  static const int TAG_UNKNOWN = 0;
  static const int TAG_ALLOC = 1;
  static const int TAG_DEALLOC = 2;
  static const int TAG_CUSTOM = 100; // Tool specific tags start here.

  StackTrace() : trace(nullptr), size(0), tag(0) {}
  StackTrace(const uptr *trace, u32 size) : trace(trace), size(size), tag(0) {}
  StackTrace(const uptr *trace, u32 size, u32 tag)
      : trace(trace), size(size), tag(tag) {}

  // Prints a symbolized stacktrace, followed by an empty line.
  void Print() const;

  // Prints a symbolized stacktrace to the output string, followed by an empty
  // line.
  void PrintTo(InternalScopedString *output) const;

  // Prints a symbolized stacktrace to the output buffer, followed by an empty
  // line. Returns the number of symbols that should have been written to buffer
  // (not including trailing '\0'). Thus, the string is truncated iff return
  // value is not less than "out_buf_size".
  uptr PrintTo(char *out_buf, uptr out_buf_size) const;

  static bool WillUseFastUnwind(bool request_fast_unwind) {
    if (!SANITIZER_CAN_FAST_UNWIND)
      return false;
    if (!SANITIZER_CAN_SLOW_UNWIND)
      return true;
    return request_fast_unwind;
  }

  static uptr GetCurrentPc();
  static inline uptr GetPreviousInstructionPc(uptr pc);
  static uptr GetNextInstructionPc(uptr pc);
};

// Performance-critical, must be in the header.
ALWAYS_INLINE
uptr StackTrace::GetPreviousInstructionPc(uptr pc) {
#if defined(__arm__)
  // T32 (Thumb) branch instructions might be 16 or 32 bit long,
  // so we return (pc-2) in that case in order to be safe.
  // For A32 mode we return (pc-4) because all instructions are 32 bit long.
  return (pc - 3) & (~1);
#elif defined(__sparc__) || defined(__mips__)
  return pc - 8;
#elif SANITIZER_RISCV64
  // RV-64 has variable instruction length...
  // C extentions gives us 2-byte instructoins
  // RV-64 has 4-byte instructions
  // + RISC-V architecture allows instructions up to 8 bytes
  // It seems difficult to figure out the exact instruction length -
  // pc - 2 seems like a safe option for the purposes of stack tracing
  return pc - 2;
#elif SANITIZER_S390 || SANITIZER_I386 || SANITIZER_X32 || SANITIZER_X64
  return pc - 1;
#else
  return pc - 4;
#endif
}

// StackTrace that owns the buffer used to store the addresses.
struct BufferedStackTrace : public StackTrace {
  uptr trace_buffer[kStackTraceMax];
  uptr top_frame_bp;  // Optional bp of a top frame.

  BufferedStackTrace() : StackTrace(trace_buffer, 0), top_frame_bp(0) {}

  void Init(const uptr *pcs, uptr cnt, uptr extra_top_pc = 0);

  // Get the stack trace with the given pc and bp.
  // The pc will be in the position 0 of the resulting stack trace.
  // The bp may refer to the current frame or to the caller's frame.
  void Unwind(uptr pc, uptr bp, void *context, bool request_fast,
              u32 max_depth = kStackTraceMax) {
    top_frame_bp = (max_depth > 0) ? bp : 0;
    // Small max_depth optimization
    if (max_depth <= 1) {
      if (max_depth == 1)
        trace_buffer[0] = pc;
      size = max_depth;
      return;
    }
    UnwindImpl(pc, bp, context, request_fast, max_depth);
  }

  void Unwind(u32 max_depth, uptr pc, uptr bp, void *context, uptr stack_top,
              uptr stack_bottom, bool request_fast_unwind);

  void Reset() {
    *static_cast<StackTrace *>(this) = StackTrace(trace_buffer, 0);
    top_frame_bp = 0;
  }

 private:
  // Every runtime defines its own implementation of this method
  void UnwindImpl(uptr pc, uptr bp, void *context, bool request_fast,
                  u32 max_depth);

  // UnwindFast/Slow have platform-specific implementations
  void UnwindFast(uptr pc, uptr bp, uptr stack_top, uptr stack_bottom,
                  u32 max_depth);
  void UnwindSlow(uptr pc, u32 max_depth);
  void UnwindSlow(uptr pc, void *context, u32 max_depth);

  void PopStackFrames(uptr count);
  uptr LocatePcInTrace(uptr pc);

  BufferedStackTrace(const BufferedStackTrace &) = delete;
  void operator=(const BufferedStackTrace &) = delete;

  friend class FastUnwindTest;
};

#if defined(__s390x__)
static const uptr kFrameSize = 160;
#elif defined(__s390__)
static const uptr kFrameSize = 96;
#else
static const uptr kFrameSize = 2 * sizeof(uhwptr);
#endif

// Check if given pointer points into allocated stack area.
static inline bool IsValidFrame(uptr frame, uptr stack_top, uptr stack_bottom) {
  return frame > stack_bottom && frame < stack_top - kFrameSize;
}

}  // namespace __sanitizer

// Use this macro if you want to print stack trace with the caller
// of the current function in the top frame.
#define GET_CALLER_PC_BP \
  uptr bp = GET_CURRENT_FRAME();              \
  uptr pc = GET_CALLER_PC();

#define GET_CALLER_PC_BP_SP \
  GET_CALLER_PC_BP;                           \
  uptr local_stack;                           \
  uptr sp = (uptr)&local_stack

// Use this macro if you want to print stack trace with the current
// function in the top frame.
#define GET_CURRENT_PC_BP \
  uptr bp = GET_CURRENT_FRAME();              \
  uptr pc = StackTrace::GetCurrentPc()

#define GET_CURRENT_PC_BP_SP \
  GET_CURRENT_PC_BP;                          \
  uptr local_stack;                           \
  uptr sp = (uptr)&local_stack

// GET_CURRENT_PC() is equivalent to StackTrace::GetCurrentPc().
// Optimized x86 version is faster than GetCurrentPc because
// it does not involve a function call, instead it reads RIP register.
// Reads of RIP by an instruction return RIP pointing to the next
// instruction, which is exactly what we want here, thus 0 offset.
// It needs to be a macro because otherwise we will get the name
// of this function on the top of most stacks. Attribute artificial
// does not do what it claims to do, unfortunatley. And attribute
// __nodebug__ is clang-only. If we would have an attribute that
// would remove this function from debug info, we could simply make
// StackTrace::GetCurrentPc() faster.
#if defined(__x86_64__)
#  define GET_CURRENT_PC()                \
    (__extension__({                      \
      uptr pc;                            \
      asm("lea 0(%%rip), %0" : "=r"(pc)); \
      pc;                                 \
    }))
#else
#  define GET_CURRENT_PC() StackTrace::GetCurrentPc()
#endif

#endif  // SANITIZER_STACKTRACE_H
