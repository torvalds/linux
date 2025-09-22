//===-- sanitizer_stoptheworld_test.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for sanitizer_stoptheworld.h
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_stoptheworld.h"

#include "sanitizer_common/sanitizer_platform.h"
#if (SANITIZER_LINUX || SANITIZER_WINDOWS) && defined(__x86_64__)

#  include <atomic>
#  include <mutex>
#  include <thread>

#  include "gtest/gtest.h"
#  include "sanitizer_common/sanitizer_common.h"
#  include "sanitizer_common/sanitizer_libc.h"

namespace __sanitizer {

static std::mutex mutex;

struct CallbackArgument {
  std::atomic_int counter = {};
  std::atomic_bool threads_stopped = {};
  std::atomic_bool callback_executed = {};
};

void IncrementerThread(CallbackArgument &callback_argument) {
  while (true) {
    callback_argument.counter++;

    if (mutex.try_lock()) {
      mutex.unlock();
      return;
    }

    std::this_thread::yield();
  }
}

// This callback checks that IncrementerThread is suspended at the time of its
// execution.
void Callback(const SuspendedThreadsList &suspended_threads_list,
              void *argument) {
  CallbackArgument *callback_argument = (CallbackArgument *)argument;
  callback_argument->callback_executed = true;
  int counter_at_init = callback_argument->counter;
  for (uptr i = 0; i < 1000; i++) {
    std::this_thread::yield();
    if (callback_argument->counter != counter_at_init) {
      callback_argument->threads_stopped = false;
      return;
    }
  }
  callback_argument->threads_stopped = true;
}

TEST(StopTheWorld, SuspendThreadsSimple) {
  CallbackArgument argument;
  std::thread thread;
  {
    std::lock_guard<std::mutex> lock(mutex);
    thread = std::thread(IncrementerThread, std::ref(argument));
    StopTheWorld(&Callback, &argument);
  }
  EXPECT_TRUE(argument.callback_executed);
  EXPECT_TRUE(argument.threads_stopped);
  // argument is on stack, so we have to wait for the incrementer thread to
  // terminate before we can return from this function.
  ASSERT_NO_THROW(thread.join());
}

// A more comprehensive test where we spawn a bunch of threads while executing
// StopTheWorld in parallel.
static const uptr kThreadCount = 50;
static const uptr kStopWorldAfter = 10;  // let this many threads spawn first

struct AdvancedCallbackArgument {
  std::atomic_uintptr_t thread_index = {};
  std::atomic_int counters[kThreadCount] = {};
  std::thread threads[kThreadCount];
  std::atomic_bool threads_stopped = {};
  std::atomic_bool callback_executed = {};
};

void AdvancedIncrementerThread(AdvancedCallbackArgument &callback_argument) {
  uptr this_thread_index = callback_argument.thread_index++;
  // Spawn the next thread.
  if (this_thread_index + 1 < kThreadCount) {
    callback_argument.threads[this_thread_index + 1] =
        std::thread(AdvancedIncrementerThread, std::ref(callback_argument));
  }
  // Do the actual work.
  while (true) {
    callback_argument.counters[this_thread_index]++;
    if (mutex.try_lock()) {
      mutex.unlock();
      return;
    }

    std::this_thread::yield();
  }
}

void AdvancedCallback(const SuspendedThreadsList &suspended_threads_list,
                      void *argument) {
  AdvancedCallbackArgument *callback_argument =
      (AdvancedCallbackArgument *)argument;
  callback_argument->callback_executed = true;

  int counters_at_init[kThreadCount];
  for (uptr j = 0; j < kThreadCount; j++)
    counters_at_init[j] = callback_argument->counters[j];
  for (uptr i = 0; i < 10; i++) {
    std::this_thread::yield();
    for (uptr j = 0; j < kThreadCount; j++)
      if (callback_argument->counters[j] != counters_at_init[j]) {
        callback_argument->threads_stopped = false;
        return;
      }
  }
  callback_argument->threads_stopped = true;
}

TEST(StopTheWorld, SuspendThreadsAdvanced) {
  AdvancedCallbackArgument argument;

  {
    std::lock_guard<std::mutex> lock(mutex);
    argument.threads[0] =
        std::thread(AdvancedIncrementerThread, std::ref(argument));
    // Wait for several threads to spawn before proceeding.
    while (argument.thread_index < kStopWorldAfter) std::this_thread::yield();
    StopTheWorld(&AdvancedCallback, &argument);
    EXPECT_TRUE(argument.callback_executed);
    EXPECT_TRUE(argument.threads_stopped);

    // Wait for all threads to spawn before we start terminating them.
    while (argument.thread_index < kThreadCount) std::this_thread::yield();
  }
  // Signal the threads to terminate.
  for (auto &t : argument.threads) t.join();
}

static void SegvCallback(const SuspendedThreadsList &suspended_threads_list,
                         void *argument) {
  *(volatile int *)0x1234 = 0;
}

#  if SANITIZER_WINDOWS
#    define MAYBE_SegvInCallback DISABLED_SegvInCallback
#  else
#    define MAYBE_SegvInCallback SegvInCallback
#  endif

TEST(StopTheWorld, MAYBE_SegvInCallback) {
  // Test that tracer thread catches SIGSEGV.
  StopTheWorld(&SegvCallback, NULL);
}

}  // namespace __sanitizer

#endif  // SANITIZER_LINUX && defined(__x86_64__)
