//===-- dd_rtl.h ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef DD_RTL_H
#define DD_RTL_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_deadlock_detector_interface.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_addrhashmap.h"
#include "sanitizer_common/sanitizer_mutex.h"

namespace __dsan {

typedef DDFlags Flags;

struct UserMutex {
  DDMutex dd;
};

struct Thread {
  DDPhysicalThread *dd_pt;
  DDLogicalThread *dd_lt;

  bool ignore_interceptors;
};

struct Callback final : public DDCallback {
  Thread *thr;

  Callback(Thread *thr);
  u32 Unwind() override;
};

typedef AddrHashMap<UserMutex, 31051> MutexHashMap;

struct Context {
  DDetector *dd;

  Mutex report_mutex;
  MutexHashMap mutex_map;
};

inline Flags* flags() {
  static Flags flags;
  return &flags;
}

void Initialize();
void InitializeInterceptors();

void ThreadInit(Thread *thr);
void ThreadDestroy(Thread *thr);

void MutexBeforeLock(Thread *thr, uptr m, bool writelock);
void MutexAfterLock(Thread *thr, uptr m, bool writelock, bool trylock);
void MutexBeforeUnlock(Thread *thr, uptr m, bool writelock);
void MutexDestroy(Thread *thr, uptr m);

}  // namespace __dsan
#endif  // DD_RTL_H
