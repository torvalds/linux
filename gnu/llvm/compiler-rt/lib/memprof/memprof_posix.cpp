//===-- memprof_posix.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Posix-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if !SANITIZER_POSIX
#error Only Posix supported
#endif

#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#include <pthread.h>

namespace __memprof {

// ---------------------- TSD ---------------- {{{1

static pthread_key_t tsd_key;
static bool tsd_key_inited = false;
void TSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

void *TSDGet() {
  CHECK(tsd_key_inited);
  return pthread_getspecific(tsd_key);
}

void TSDSet(void *tsd) {
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, tsd);
}

void PlatformTSDDtor(void *tsd) {
  MemprofThreadContext *context = (MemprofThreadContext *)tsd;
  if (context->destructor_iterations > 1) {
    context->destructor_iterations--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
  MemprofThread::TSDDtor(tsd);
}
} // namespace __memprof
