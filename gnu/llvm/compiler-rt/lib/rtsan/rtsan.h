//===--- rtsan.h - Realtime Sanitizer ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "sanitizer_common/sanitizer_internal_defs.h"

extern "C" {

namespace __rtsan {

extern bool rtsan_initialized;
extern bool rtsan_init_is_running;

} // namespace __rtsan

// Initialise rtsan interceptors.
// A call to this method is added to the preinit array on Linux systems.
SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_init();

// Enter real-time context.
// When in a real-time context, RTSan interceptors will error if realtime
// violations are detected. Calls to this method are injected at the code
// generation stage when RTSan is enabled.
SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_realtime_enter();

// Exit the real-time context.
// When not in a real-time context, RTSan interceptors will simply forward
// intercepted method calls to the real methods.
SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_realtime_exit();

// Disable all RTSan error reporting.
// Injected into the code if "nosanitize(realtime)" is on a function.
SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_off();

// Re-enable all RTSan error reporting.
// The counterpart to `__rtsan_off`.
SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_on();

} // extern "C"
