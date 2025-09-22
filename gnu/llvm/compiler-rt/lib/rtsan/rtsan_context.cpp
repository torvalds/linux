//===--- rtsan_context.cpp - Realtime Sanitizer -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include <rtsan/rtsan_context.h>

#include <rtsan/rtsan_stack.h>

#include <sanitizer_common/sanitizer_allocator_internal.h>
#include <sanitizer_common/sanitizer_stacktrace.h>

#include <new>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_key_t context_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

// InternalFree cannot be passed directly to pthread_key_create
// because it expects a signature with only one arg
static void InternalFreeWrapper(void *ptr) { __sanitizer::InternalFree(ptr); }

static __rtsan::Context &GetContextForThisThreadImpl() {
  auto make_thread_local_context_key = []() {
    CHECK_EQ(pthread_key_create(&context_key, InternalFreeWrapper), 0);
  };

  pthread_once(&key_once, make_thread_local_context_key);
  __rtsan::Context *current_thread_context =
      static_cast<__rtsan::Context *>(pthread_getspecific(context_key));
  if (current_thread_context == nullptr) {
    current_thread_context = static_cast<__rtsan::Context *>(
        __sanitizer::InternalAlloc(sizeof(__rtsan::Context)));
    new (current_thread_context) __rtsan::Context();
    pthread_setspecific(context_key, current_thread_context);
  }

  return *current_thread_context;
}

/*
    This is a placeholder stub for a future feature that will allow
    a user to configure RTSan's behaviour when a real-time safety
    violation is detected. The RTSan developers intend for the
    following choices to be made available, via a RTSAN_OPTIONS
    environment variable, in a future PR:

        i) exit,
       ii) continue, or
      iii) wait for user input from stdin.

    Until then, and to keep the first PRs small, only the exit mode
    is available.
*/
static void InvokeViolationDetectedAction() { exit(EXIT_FAILURE); }

__rtsan::Context::Context() = default;

void __rtsan::Context::RealtimePush() { realtime_depth++; }

void __rtsan::Context::RealtimePop() { realtime_depth--; }

void __rtsan::Context::BypassPush() { bypass_depth++; }

void __rtsan::Context::BypassPop() { bypass_depth--; }

void __rtsan::Context::ExpectNotRealtime(
    const char *intercepted_function_name) {
  if (InRealtimeContext() && !IsBypassed()) {
    BypassPush();
    PrintDiagnostics(intercepted_function_name);
    InvokeViolationDetectedAction();
    BypassPop();
  }
}

bool __rtsan::Context::InRealtimeContext() const { return realtime_depth > 0; }

bool __rtsan::Context::IsBypassed() const { return bypass_depth > 0; }

void __rtsan::Context::PrintDiagnostics(const char *intercepted_function_name) {
  fprintf(stderr,
          "Real-time violation: intercepted call to real-time unsafe function "
          "`%s` in real-time context! Stack trace:\n",
          intercepted_function_name);
  __rtsan::PrintStackTrace();
}

__rtsan::Context &__rtsan::GetContextForThisThread() {
  return GetContextForThisThreadImpl();
}
