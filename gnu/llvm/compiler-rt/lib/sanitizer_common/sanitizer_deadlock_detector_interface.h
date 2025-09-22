//===-- sanitizer_deadlock_detector_interface.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer runtime.
// Abstract deadlock detector interface.
// FIXME: this is work in progress, nothing really works yet.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_DEADLOCK_DETECTOR_INTERFACE_H
#define SANITIZER_DEADLOCK_DETECTOR_INTERFACE_H

#ifndef SANITIZER_DEADLOCK_DETECTOR_VERSION
# define SANITIZER_DEADLOCK_DETECTOR_VERSION 1
#endif

#include "sanitizer_internal_defs.h"
#include "sanitizer_atomic.h"

namespace __sanitizer {

// dd - deadlock detector.
// lt - logical (user) thread.
// pt - physical (OS) thread.

struct DDPhysicalThread;
struct DDLogicalThread;

struct DDMutex {
#if SANITIZER_DEADLOCK_DETECTOR_VERSION == 1
  uptr id;
  u32  stk;  // creation stack
#elif SANITIZER_DEADLOCK_DETECTOR_VERSION == 2
  u32              id;
  u32              recursion;
  atomic_uintptr_t owner;
#else
# error "BAD SANITIZER_DEADLOCK_DETECTOR_VERSION"
#endif
  u64  ctx;
};

struct DDFlags {
  bool second_deadlock_stack;
};

struct DDReport {
  enum { kMaxLoopSize = 20 };
  int n;  // number of entries in loop
  struct {
    u64 thr_ctx;   // user thread context
    u64 mtx_ctx0;  // user mutex context, start of the edge
    u64 mtx_ctx1;  // user mutex context, end of the edge
    u32 stk[2];  // stack ids for the edge
  } loop[kMaxLoopSize];
};

struct DDCallback {
  DDPhysicalThread *pt;
  DDLogicalThread  *lt;

  virtual u32 Unwind() { return 0; }
  virtual int UniqueTid() { return 0; }

 protected:
  ~DDCallback() {}
};

struct DDetector {
  static DDetector *Create(const DDFlags *flags);

  virtual DDPhysicalThread* CreatePhysicalThread() { return nullptr; }
  virtual void DestroyPhysicalThread(DDPhysicalThread *pt) {}

  virtual DDLogicalThread* CreateLogicalThread(u64 ctx) { return nullptr; }
  virtual void DestroyLogicalThread(DDLogicalThread *lt) {}

  virtual void MutexInit(DDCallback *cb, DDMutex *m) {}
  virtual void MutexBeforeLock(DDCallback *cb, DDMutex *m, bool wlock) {}
  virtual void MutexAfterLock(DDCallback *cb, DDMutex *m, bool wlock,
      bool trylock) {}
  virtual void MutexBeforeUnlock(DDCallback *cb, DDMutex *m, bool wlock) {}
  virtual void MutexDestroy(DDCallback *cb, DDMutex *m) {}

  virtual DDReport *GetReport(DDCallback *cb) { return nullptr; }

 protected:
  ~DDetector() {}
};

} // namespace __sanitizer

#endif // SANITIZER_DEADLOCK_DETECTOR_INTERFACE_H
