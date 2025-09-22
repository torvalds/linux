//=-- lsan.h --------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Private header for standalone LSan RTL.
//
//===----------------------------------------------------------------------===//

#include "lsan_thread.h"
#if SANITIZER_POSIX
#  include "lsan_posix.h"
#elif SANITIZER_FUCHSIA
#  include "lsan_fuchsia.h"
#endif
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

#define GET_STACK_TRACE(max_size, fast)                                        \
  __sanitizer::BufferedStackTrace stack;                                       \
  stack.Unwind(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(), nullptr, fast, \
               max_size);

#define GET_STACK_TRACE_FATAL \
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_fatal)

#define GET_STACK_TRACE_MALLOC                                      \
  GET_STACK_TRACE(__sanitizer::common_flags()->malloc_context_size, \
                  common_flags()->fast_unwind_on_malloc)

#define GET_STACK_TRACE_THREAD GET_STACK_TRACE(kStackTraceMax, true)

namespace __lsan {

void InitializeInterceptors();
void ReplaceSystemMalloc();
void LsanOnDeadlySignal(int signo, void *siginfo, void *context);
void InstallAtExitCheckLeaks();
void InstallAtForkHandler();

#define ENSURE_LSAN_INITED        \
  do {                            \
    CHECK(!lsan_init_is_running); \
    if (!lsan_inited)             \
      __lsan_init();              \
  } while (0)

}  // namespace __lsan

extern bool lsan_inited;
extern bool lsan_init_is_running;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __lsan_init();
