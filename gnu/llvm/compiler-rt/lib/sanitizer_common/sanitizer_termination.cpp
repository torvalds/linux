//===-- sanitizer_termination.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file contains the Sanitizer termination functions CheckFailed and Die,
/// and the callback functionalities associated with them.
///
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_libc.h"

namespace __sanitizer {

static const int kMaxNumOfInternalDieCallbacks = 5;
static DieCallbackType InternalDieCallbacks[kMaxNumOfInternalDieCallbacks];

bool AddDieCallback(DieCallbackType callback) {
  for (int i = 0; i < kMaxNumOfInternalDieCallbacks; i++) {
    if (InternalDieCallbacks[i] == nullptr) {
      InternalDieCallbacks[i] = callback;
      return true;
    }
  }
  return false;
}

bool RemoveDieCallback(DieCallbackType callback) {
  for (int i = 0; i < kMaxNumOfInternalDieCallbacks; i++) {
    if (InternalDieCallbacks[i] == callback) {
      internal_memmove(&InternalDieCallbacks[i], &InternalDieCallbacks[i + 1],
                       sizeof(InternalDieCallbacks[0]) *
                           (kMaxNumOfInternalDieCallbacks - i - 1));
      InternalDieCallbacks[kMaxNumOfInternalDieCallbacks - 1] = nullptr;
      return true;
    }
  }
  return false;
}

static DieCallbackType UserDieCallback;
void SetUserDieCallback(DieCallbackType callback) {
  UserDieCallback = callback;
}

void NORETURN Die() {
  if (UserDieCallback)
    UserDieCallback();
  for (int i = kMaxNumOfInternalDieCallbacks - 1; i >= 0; i--) {
    if (InternalDieCallbacks[i])
      InternalDieCallbacks[i]();
  }
  if (common_flags()->abort_on_error)
    Abort();
  internal__exit(common_flags()->exitcode);
}

static void (*CheckUnwindCallback)();
void SetCheckUnwindCallback(void (*callback)()) {
  CheckUnwindCallback = callback;
}

void NORETURN CheckFailed(const char *file, int line, const char *cond,
                          u64 v1, u64 v2) {
  u32 tid = GetTid();
  Printf("%s: CHECK failed: %s:%d \"%s\" (0x%zx, 0x%zx) (tid=%u)\n",
         SanitizerToolName, StripModuleName(file), line, cond, (uptr)v1,
         (uptr)v2, tid);
  static atomic_uint32_t first_tid;
  u32 cmp = 0;
  if (!atomic_compare_exchange_strong(&first_tid, &cmp, tid,
                                      memory_order_relaxed)) {
    if (cmp == tid) {
      // Recursing into CheckFailed.
    } else {
      // Another thread fails already, let it print the stack and terminate.
      SleepForSeconds(2);
    }
    Trap();
  }
  if (CheckUnwindCallback)
    CheckUnwindCallback();
  Die();
}

} // namespace __sanitizer

using namespace __sanitizer;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_set_death_callback(void (*callback)(void)) {
  SetUserDieCallback(callback);
}
}  // extern "C"
