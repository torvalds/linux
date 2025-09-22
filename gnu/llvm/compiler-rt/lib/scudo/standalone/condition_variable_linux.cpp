//===-- condition_variable_linux.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_LINUX

#include "condition_variable_linux.h"

#include "atomic_helpers.h"

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace scudo {

void ConditionVariableLinux::notifyAllImpl(UNUSED HybridMutex &M) {
  const u32 V = atomic_load_relaxed(&Counter);
  atomic_store_relaxed(&Counter, V + 1);

  // TODO(chiahungduan): Move the waiters from the futex waiting queue
  // `Counter` to futex waiting queue `M` so that the awoken threads won't be
  // blocked again due to locked `M` by current thread.
  if (LastNotifyAll != V) {
    syscall(SYS_futex, reinterpret_cast<uptr>(&Counter), FUTEX_WAKE_PRIVATE,
            INT_MAX, nullptr, nullptr, 0);
  }

  LastNotifyAll = V + 1;
}

void ConditionVariableLinux::waitImpl(HybridMutex &M) {
  const u32 V = atomic_load_relaxed(&Counter) + 1;
  atomic_store_relaxed(&Counter, V);

  // TODO: Use ScopedUnlock when it's supported.
  M.unlock();
  syscall(SYS_futex, reinterpret_cast<uptr>(&Counter), FUTEX_WAIT_PRIVATE, V,
          nullptr, nullptr, 0);
  M.lock();
}

} // namespace scudo

#endif // SCUDO_LINUX
