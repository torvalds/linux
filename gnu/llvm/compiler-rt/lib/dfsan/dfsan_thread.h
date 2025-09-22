//===-- dfsan_thread.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef DFSAN_THREAD_H
#define DFSAN_THREAD_H

#include "dfsan_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_posix.h"

namespace __dfsan {

class DFsanThread {
 public:
  // NOTE: There is no DFsanThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.

  static DFsanThread *Create(thread_callback_t start_routine, void *arg,
                             bool track_origins = false);
  static void TSDDtor(void *tsd);
  void Destroy();

  void Init();  // Should be called from the thread itself.
  thread_return_t ThreadStart();

  uptr stack_top();
  uptr stack_bottom();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  bool IsMainThread() { return start_routine_ == nullptr; }

  bool InSignalHandler() { return in_signal_handler_; }
  void EnterSignalHandler() { in_signal_handler_++; }
  void LeaveSignalHandler() { in_signal_handler_--; }

  DFsanThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }

  int destructor_iterations_;
  __sanitizer_sigset_t starting_sigset_;

 private:
  void SetThreadStackAndTls();
  void ClearShadowForThreadStackAndTLS();
  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;

  bool AddrIsInStack(uptr addr);

  thread_callback_t start_routine_;
  void *arg_;
  bool track_origins_;

  StackBounds stack_;

  uptr tls_begin_;
  uptr tls_end_;

  unsigned in_signal_handler_;

  DFsanThreadLocalMallocStorage malloc_storage_;
};

DFsanThread *GetCurrentThread();
void SetCurrentThread(DFsanThread *t);
void DFsanTSDInit(void (*destructor)(void *tsd));
void DFsanTSDDtor(void *tsd);

}  // namespace __dfsan

#endif  // DFSAN_THREAD_H
