//===-- sanitizer_stoptheworld_testlib.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Dynamic library to test StopTheWorld functionality.
// When loaded with LD_PRELOAD, it will periodically suspend all threads.
//===----------------------------------------------------------------------===//
/* Usage:
clang++ -fno-exceptions -g -fPIC -I. \
 sanitizer_common/tests/sanitizer_stoptheworld_testlib.cpp \
 sanitizer_common/sanitizer_*.cpp -shared -lpthread -o teststoptheworld.so
LD_PRELOAD=`pwd`/teststoptheworld.so /your/app
*/

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "sanitizer_common/sanitizer_stoptheworld.h"

namespace {
const uptr kSuspendDuration = 3;
const uptr kRunDuration = 3;

void Callback(const SuspendedThreadsList &suspended_threads_list,
              void *argument) {
  sleep(kSuspendDuration);
}

void *SuspenderThread(void *argument) {
  while (true) {
    sleep(kRunDuration);
    StopTheWorld(Callback, NULL);
  }
  return NULL;
}

__attribute__((constructor)) void StopTheWorldTestLibConstructor(void) {
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, SuspenderThread, NULL);
}
}  // namespace

#endif  // SANITIZER_LINUX
