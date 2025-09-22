//===-- sanitizer_symbolizer_report_fuchsia.cpp
//-----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the report functions for fuchsia.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_SYMBOLIZER_MARKUP

#  include "sanitizer_common.h"

namespace __sanitizer {
void StartReportDeadlySignal() {}

void ReportDeadlySignal(const SignalContext &sig, u32 tid,
                        UnwindSignalStackCallbackType unwind,
                        const void *unwind_context) {}

void HandleDeadlySignal(void *siginfo, void *context, u32 tid,
                        UnwindSignalStackCallbackType unwind,
                        const void *unwind_context) {}

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_MARKUP
