//===-- mutex_fuchsia.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/mutex.h"

#include <lib/sync/mutex.h>

namespace gwp_asan {
void Mutex::lock() __TA_NO_THREAD_SAFETY_ANALYSIS { sync_mutex_lock(&Mu); }

bool Mutex::tryLock() __TA_NO_THREAD_SAFETY_ANALYSIS {
  return sync_mutex_trylock(&Mu) == ZX_OK;
}

void Mutex::unlock() __TA_NO_THREAD_SAFETY_ANALYSIS { sync_mutex_unlock(&Mu); }
} // namespace gwp_asan
