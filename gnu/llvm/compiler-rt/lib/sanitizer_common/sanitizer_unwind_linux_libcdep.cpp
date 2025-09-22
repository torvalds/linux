//===-- sanitizer_unwind_linux_libcdep.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the unwind.h-based (aka "slow") stack unwinding routines
// available to the tools on Linux, Android, NetBSD, FreeBSD, and Solaris.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_SOLARIS
#include "sanitizer_common.h"
#include "sanitizer_stacktrace.h"

#if SANITIZER_ANDROID
#include <dlfcn.h>  // for dlopen()
#endif

#if SANITIZER_FREEBSD
#define _GNU_SOURCE  // to declare _Unwind_Backtrace() from <unwind.h>
#endif
#include <unwind.h>

namespace __sanitizer {

namespace {

//---------------------------- UnwindSlow --------------------------------------

typedef struct {
  uptr absolute_pc;
  uptr stack_top;
  uptr stack_size;
} backtrace_frame_t;

extern "C" {
typedef void *(*acquire_my_map_info_list_func)();
typedef void (*release_my_map_info_list_func)(void *map);
typedef sptr (*unwind_backtrace_signal_arch_func)(
    void *siginfo, void *sigcontext, void *map_info_list,
    backtrace_frame_t *backtrace, uptr ignore_depth, uptr max_depth);
acquire_my_map_info_list_func acquire_my_map_info_list;
release_my_map_info_list_func release_my_map_info_list;
unwind_backtrace_signal_arch_func unwind_backtrace_signal_arch;
} // extern "C"

#if defined(__arm__) && !SANITIZER_NETBSD
// NetBSD uses dwarf EH
#define UNWIND_STOP _URC_END_OF_STACK
#define UNWIND_CONTINUE _URC_NO_REASON
#else
#define UNWIND_STOP _URC_NORMAL_STOP
#define UNWIND_CONTINUE _URC_NO_REASON
#endif

uptr Unwind_GetIP(struct _Unwind_Context *ctx) {
#if defined(__arm__) && !SANITIZER_APPLE
  uptr val;
  _Unwind_VRS_Result res = _Unwind_VRS_Get(ctx, _UVRSC_CORE,
      15 /* r15 = PC */, _UVRSD_UINT32, &val);
  CHECK(res == _UVRSR_OK && "_Unwind_VRS_Get failed");
  // Clear the Thumb bit.
  return val & ~(uptr)1;
#else
  return (uptr)_Unwind_GetIP(ctx);
#endif
}

struct UnwindTraceArg {
  BufferedStackTrace *stack;
  u32 max_depth;
};

_Unwind_Reason_Code Unwind_Trace(struct _Unwind_Context *ctx, void *param) {
  UnwindTraceArg *arg = (UnwindTraceArg*)param;
  CHECK_LT(arg->stack->size, arg->max_depth);
  uptr pc = Unwind_GetIP(ctx);
  const uptr kPageSize = GetPageSizeCached();
  // Let's assume that any pointer in the 0th page (i.e. <0x1000 on i386 and
  // x86_64) is invalid and stop unwinding here.  If we're adding support for
  // a platform where this isn't true, we need to reconsider this check.
  if (pc < kPageSize) return UNWIND_STOP;
  arg->stack->trace_buffer[arg->stack->size++] = pc;
  if (arg->stack->size == arg->max_depth) return UNWIND_STOP;
  return UNWIND_CONTINUE;
}

}  // namespace

#if SANITIZER_ANDROID
void SanitizerInitializeUnwinder() {
  if (AndroidGetApiLevel() >= ANDROID_LOLLIPOP_MR1) return;

  // Pre-lollipop Android can not unwind through signal handler frames with
  // libgcc unwinder, but it has a libcorkscrew.so library with the necessary
  // workarounds.
  void *p = dlopen("libcorkscrew.so", RTLD_LAZY);
  if (!p) {
    VReport(1,
            "Failed to open libcorkscrew.so. You may see broken stack traces "
            "in SEGV reports.");
    return;
  }
  acquire_my_map_info_list =
      (acquire_my_map_info_list_func)(uptr)dlsym(p, "acquire_my_map_info_list");
  release_my_map_info_list =
      (release_my_map_info_list_func)(uptr)dlsym(p, "release_my_map_info_list");
  unwind_backtrace_signal_arch = (unwind_backtrace_signal_arch_func)(uptr)dlsym(
      p, "unwind_backtrace_signal_arch");
  if (!acquire_my_map_info_list || !release_my_map_info_list ||
      !unwind_backtrace_signal_arch) {
    VReport(1,
            "Failed to find one of the required symbols in libcorkscrew.so. "
            "You may see broken stack traces in SEGV reports.");
    acquire_my_map_info_list = 0;
    unwind_backtrace_signal_arch = 0;
    release_my_map_info_list = 0;
  }
}
#endif

void BufferedStackTrace::UnwindSlow(uptr pc, u32 max_depth) {
  CHECK_GE(max_depth, 2);
  size = 0;
  UnwindTraceArg arg = {this, Min(max_depth + 1, kStackTraceMax)};
  _Unwind_Backtrace(Unwind_Trace, &arg);
  // We need to pop a few frames so that pc is on top.
  uptr to_pop = LocatePcInTrace(pc);
  // trace_buffer[0] belongs to the current function so we always pop it,
  // unless there is only 1 frame in the stack trace (1 frame is always better
  // than 0!).
  // 1-frame stacks don't normally happen, but this depends on the actual
  // unwinder implementation (libgcc, libunwind, etc) which is outside of our
  // control.
  if (to_pop == 0 && size > 1)
    to_pop = 1;
  PopStackFrames(to_pop);
  trace_buffer[0] = pc;
}

void BufferedStackTrace::UnwindSlow(uptr pc, void *context, u32 max_depth) {
  CHECK(context);
  CHECK_GE(max_depth, 2);
  if (!unwind_backtrace_signal_arch) {
    UnwindSlow(pc, max_depth);
    return;
  }

  void *map = acquire_my_map_info_list();
  CHECK(map);
  InternalMmapVector<backtrace_frame_t> frames(kStackTraceMax);
  // siginfo argument appears to be unused.
  sptr res = unwind_backtrace_signal_arch(/* siginfo */ 0, context, map,
                                          frames.data(),
                                          /* ignore_depth */ 0, max_depth);
  release_my_map_info_list(map);
  if (res < 0) return;
  CHECK_LE((uptr)res, kStackTraceMax);

  size = 0;
  // +2 compensate for libcorkscrew unwinder returning addresses of call
  // instructions instead of raw return addresses.
  for (sptr i = 0; i < res; ++i)
    trace_buffer[size++] = frames[i].absolute_pc + 2;
}

}  // namespace __sanitizer

#endif  // SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD ||
        // SANITIZER_SOLARIS
