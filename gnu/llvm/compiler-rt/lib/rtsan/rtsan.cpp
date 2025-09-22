//===--- rtsan.cpp - Realtime Sanitizer -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include <rtsan/rtsan.h>
#include <rtsan/rtsan_context.h>
#include <rtsan/rtsan_interceptors.h>

using namespace __rtsan;

bool __rtsan::rtsan_initialized;
bool __rtsan::rtsan_init_is_running;

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_init() {
  CHECK(!rtsan_init_is_running);
  if (rtsan_initialized)
    return;
  rtsan_init_is_running = true;

  InitializeInterceptors();

  rtsan_init_is_running = false;
  rtsan_initialized = true;
}

SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_realtime_enter() {
  __rtsan::GetContextForThisThread().RealtimePush();
}

SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_realtime_exit() {
  __rtsan::GetContextForThisThread().RealtimePop();
}

SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_off() {
  __rtsan::GetContextForThisThread().BypassPush();
}

SANITIZER_INTERFACE_ATTRIBUTE void __rtsan_on() {
  __rtsan::GetContextForThisThread().BypassPop();
}

} // extern "C"
