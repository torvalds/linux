//=-- ubsan_signals_standalone.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Installs signal handlers and related interceptors for UBSan standalone.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#include "sanitizer_common/sanitizer_platform.h"
#if CAN_SANITIZE_UB
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "ubsan_diag.h"
#include "ubsan_init.h"

// Interception of signals breaks too many things on Android.
// * It requires that ubsan is the first dependency of the main executable for
// the interceptors to work correctly. This complicates deployment, as it
// prevents us from enabling ubsan on random platform modules independently.
// * For this to work with ART VM, ubsan signal handler has to be set after the
// debuggerd handler, but before the ART handler.
// * Interceptors don't work at all when ubsan runtime is loaded late, ex. when
// it is part of an APK that does not use wrap.sh method.
#if SANITIZER_FUCHSIA || SANITIZER_ANDROID

namespace __ubsan {
void InitializeDeadlySignals() {}
}

#else

namespace __ubsan {
void InitializeDeadlySignals();
} // namespace __ubsan

#define COMMON_INTERCEPT_FUNCTION(name) INTERCEPT_FUNCTION(name)
#define SIGNAL_INTERCEPTOR_ENTER() __ubsan::InitializeDeadlySignals()
#include "sanitizer_common/sanitizer_signal_interceptors.inc"

// TODO(yln): Temporary workaround. Will be removed.
void ubsan_GetStackTrace(BufferedStackTrace *stack, uptr max_depth,
                         uptr pc, uptr bp, void *context, bool fast);

namespace __ubsan {

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  ubsan_GetStackTrace(stack, kStackTraceMax,
                      StackTrace::GetNextInstructionPc(sig.pc), sig.bp,
                      sig.context, common_flags()->fast_unwind_on_fatal);
}

static void UBsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetTid(), &OnStackUnwind, nullptr);
}

static bool is_initialized = false;

void InitializeDeadlySignals() {
  if (is_initialized)
    return;
  is_initialized = true;
  InitializeSignalInterceptors();
#if SANITIZER_INTERCEPT_SIGNAL_AND_SIGACTION
  // REAL(sigaction_symname) is nullptr in a static link. Bail out.
  if (!REAL(sigaction_symname))
    return;
#endif
  InstallDeadlySignalHandlers(&UBsanOnDeadlySignal);
}

} // namespace __ubsan

#endif

#endif // CAN_SANITIZE_UB
