//===-- tsan_spinlock_defs_mac.h -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Mac-specific forward-declared function defintions that may be
// deprecated in later versions of the OS.
// These are needed for interceptors.
//
//===----------------------------------------------------------------------===//

#if SANITIZER_APPLE

#ifndef TSAN_SPINLOCK_DEFS_MAC_H
#define TSAN_SPINLOCK_DEFS_MAC_H

#include <stdint.h>

extern "C" {

/*
Provides forward declarations related to OSSpinLocks on Darwin. These functions are
deprecated on macOS version 10.12 and later,
and are no longer included in the system headers.

However, the symbols are still available on the system, so we provide these forward
declarations to prevent compilation errors in tsan_interceptors_mac.cpp, which
references these functions when defining TSAN interceptor functions.
*/

typedef int32_t OSSpinLock;

void OSSpinLockLock(volatile OSSpinLock *__lock);
void OSSpinLockUnlock(volatile OSSpinLock *__lock);
bool OSSpinLockTry(volatile OSSpinLock *__lock);

}

#endif //TSAN_SPINLOCK_DEFS_MAC_H
#endif // SANITIZER_APPLE
