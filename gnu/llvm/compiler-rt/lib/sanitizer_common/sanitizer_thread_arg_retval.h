//===-- sanitizer_thread_arg_retval.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizer tools.
//
// Tracks thread arguments and return value for leak checking.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_THREAD_ARG_RETVAL_H
#define SANITIZER_THREAD_ARG_RETVAL_H

#include "sanitizer_common.h"
#include "sanitizer_dense_map.h"
#include "sanitizer_list.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

// Primary goal of the class is to keep alive arg and retval pointer for leak
// checking. However it can be used to pass those pointer into wrappers used by
// interceptors. The difference from ThreadRegistry/ThreadList is that this
// class keeps data up to the detach or join, as exited thread still can be
// joined to retrive retval. ThreadRegistry/ThreadList can discard exited
// threads immediately.
class SANITIZER_MUTEX ThreadArgRetval {
 public:
  struct Args {
    void* (*routine)(void*);
    void* arg_retval;  // Either arg or retval.
  };
  void Lock() SANITIZER_ACQUIRE() { mtx_.Lock(); }
  void CheckLocked() const SANITIZER_CHECK_LOCKED() { mtx_.CheckLocked(); }
  void Unlock() SANITIZER_RELEASE() { mtx_.Unlock(); }

  // Wraps pthread_create or similar. We need to keep object locked, to
  // prevent child thread from proceeding without thread handle.
  template <typename CreateFn /* returns thread id on success, or 0 */>
  void Create(bool detached, const Args& args, const CreateFn& fn) {
    // No need to track detached threads with no args, but we will to do as it's
    // not expensive and less edge-cases.
    __sanitizer::Lock lock(&mtx_);
    if (uptr thread = fn())
      CreateLocked(thread, detached, args);
  }

  // Returns thread arg and routine.
  Args GetArgs(uptr thread) const;

  // Mark thread as done and stores retval or remove if detached. Should be
  // called by the thread.
  void Finish(uptr thread, void* retval);

  // Mark thread as detached or remove if done.
  template <typename DetachFn /* returns true on success */>
  void Detach(uptr thread, const DetachFn& fn) {
    // Lock to prevent re-use of the thread between fn() and DetachLocked()
    // calls.
    __sanitizer::Lock lock(&mtx_);
    if (fn())
      DetachLocked(thread);
  }

  // Joins the thread.
  template <typename JoinFn /* returns true on success */>
  void Join(uptr thread, const JoinFn& fn) {
    // Remember internal id of the thread to prevent re-use of the thread
    // between fn() and AfterJoin() calls. Locking JoinFn, like in
    // Detach(), implementation can cause deadlock.
    auto gen = BeforeJoin(thread);
    if (fn())
      AfterJoin(thread, gen);
  }

  // Returns all arg and retval which are considered alive.
  void GetAllPtrsLocked(InternalMmapVector<uptr>* ptrs);

  uptr size() const {
    __sanitizer::Lock lock(&mtx_);
    return data_.size();
  }

  // FIXME: Add fork support. Expected users of the class are sloppy with forks
  // anyway. We likely should lock/unlock the object to avoid deadlocks, and
  // erase all but the current threads, so we can detect leaked arg or retval in
  // child process.

  // FIXME: Add cancelation support. Now if a thread was canceled, the class
  // will keep pointers alive forever, missing leaks caused by cancelation.

 private:
  static const u32 kInvalidGen = UINT32_MAX;
  struct Data {
    Args args;
    u32 gen;  // Avoid collision if thread id re-used.
    bool detached;
    bool done;
  };

  void CreateLocked(uptr thread, bool detached, const Args& args);
  u32 BeforeJoin(uptr thread) const;
  void AfterJoin(uptr thread, u32 gen);
  void DetachLocked(uptr thread);

  mutable Mutex mtx_;

  DenseMap<uptr, Data> data_;
  u32 gen_ = 0;
};

}  // namespace __sanitizer

#endif  // SANITIZER_THREAD_ARG_RETVAL_H
