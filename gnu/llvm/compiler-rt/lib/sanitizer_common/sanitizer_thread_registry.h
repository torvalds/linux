//===-- sanitizer_thread_registry.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizer tools.
//
// General thread bookkeeping functionality.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_THREAD_REGISTRY_H
#define SANITIZER_THREAD_REGISTRY_H

#include "sanitizer_common.h"
#include "sanitizer_dense_map.h"
#include "sanitizer_list.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

enum ThreadStatus {
  ThreadStatusInvalid,   // Non-existent thread, data is invalid.
  ThreadStatusCreated,   // Created but not yet running.
  ThreadStatusRunning,   // The thread is currently running.
  ThreadStatusFinished,  // Joinable thread is finished but not yet joined.
  ThreadStatusDead       // Joined, but some info is still available.
};

enum class ThreadType {
  Regular, // Normal thread
  Worker,  // macOS Grand Central Dispatch (GCD) worker thread
  Fiber,   // Fiber
};

// Generic thread context. Specific sanitizer tools may inherit from it.
// If thread is dead, context may optionally be reused for a new thread.
class ThreadContextBase {
 public:
  explicit ThreadContextBase(u32 tid);
  const u32 tid;  // Thread ID. Main thread should have tid = 0.
  u64 unique_id;  // Unique thread ID.
  u32 reuse_count;  // Number of times this tid was reused.
  tid_t os_id;     // PID (used for reporting).
  uptr user_id;   // Some opaque user thread id (e.g. pthread_t).
  char name[64];  // As annotated by user.

  ThreadStatus status;
  bool detached;
  ThreadType thread_type;

  u32 parent_tid;
  ThreadContextBase *next;  // For storing thread contexts in a list.

  atomic_uint32_t thread_destroyed; // To address race of Joined vs Finished

  void SetName(const char *new_name);

  void SetDead();
  void SetJoined(void *arg);
  void SetFinished();
  void SetStarted(tid_t _os_id, ThreadType _thread_type, void *arg);
  void SetCreated(uptr _user_id, u64 _unique_id, bool _detached,
                  u32 _parent_tid, void *arg);
  void Reset();

  void SetDestroyed();
  bool GetDestroyed();

  // The following methods may be overriden by subclasses.
  // Some of them take opaque arg that may be optionally be used
  // by subclasses.
  virtual void OnDead() {}
  virtual void OnJoined(void *arg) {}
  virtual void OnFinished() {}
  virtual void OnStarted(void *arg) {}
  virtual void OnCreated(void *arg) {}
  virtual void OnReset() {}
  virtual void OnDetached(void *arg) {}

 protected:
  ~ThreadContextBase();
};

typedef ThreadContextBase* (*ThreadContextFactory)(u32 tid);

class SANITIZER_MUTEX ThreadRegistry {
 public:
  ThreadRegistry(ThreadContextFactory factory);
  ThreadRegistry(ThreadContextFactory factory, u32 max_threads,
                 u32 thread_quarantine_size, u32 max_reuse);
  void GetNumberOfThreads(uptr *total = nullptr, uptr *running = nullptr,
                          uptr *alive = nullptr);
  uptr GetMaxAliveThreads();

  void Lock() SANITIZER_ACQUIRE() { mtx_.Lock(); }
  void CheckLocked() const SANITIZER_CHECK_LOCKED() { mtx_.CheckLocked(); }
  void Unlock() SANITIZER_RELEASE() { mtx_.Unlock(); }

  // Should be guarded by ThreadRegistryLock.
  ThreadContextBase *GetThreadLocked(u32 tid) {
    return threads_.empty() ? nullptr : threads_[tid];
  }

  u32 NumThreadsLocked() const { return threads_.size(); }

  u32 CreateThread(uptr user_id, bool detached, u32 parent_tid, void *arg);

  typedef void (*ThreadCallback)(ThreadContextBase *tctx, void *arg);
  // Invokes callback with a specified arg for each thread context.
  // Should be guarded by ThreadRegistryLock.
  void RunCallbackForEachThreadLocked(ThreadCallback cb, void *arg);

  typedef bool (*FindThreadCallback)(ThreadContextBase *tctx, void *arg);
  // Finds a thread using the provided callback. Returns kInvalidTid if no
  // thread is found.
  u32 FindThread(FindThreadCallback cb, void *arg);
  // Should be guarded by ThreadRegistryLock. Return 0 if no thread
  // is found.
  ThreadContextBase *FindThreadContextLocked(FindThreadCallback cb,
                                             void *arg);
  ThreadContextBase *FindThreadContextByOsIDLocked(tid_t os_id);

  void SetThreadName(u32 tid, const char *name);
  void SetThreadNameByUserId(uptr user_id, const char *name);
  void DetachThread(u32 tid, void *arg);
  void JoinThread(u32 tid, void *arg);
  // Finishes thread and returns previous status.
  ThreadStatus FinishThread(u32 tid);
  void StartThread(u32 tid, tid_t os_id, ThreadType thread_type, void *arg);
  u32 ConsumeThreadUserId(uptr user_id);
  void SetThreadUserId(u32 tid, uptr user_id);

  // OnFork must be called in the child process after fork to purge old
  // threads that don't exist anymore (except for the current thread tid).
  // Returns number of alive threads before fork.
  u32 OnFork(u32 tid);

 private:
  const ThreadContextFactory context_factory_;
  const u32 max_threads_;
  const u32 thread_quarantine_size_;
  const u32 max_reuse_;

  Mutex mtx_;

  u64 total_threads_;   // Total number of created threads. May be greater than
                        // max_threads_ if contexts were reused.
  uptr alive_threads_;  // Created or running.
  uptr max_alive_threads_;
  uptr running_threads_;

  InternalMmapVector<ThreadContextBase *> threads_;
  IntrusiveList<ThreadContextBase> dead_threads_;
  IntrusiveList<ThreadContextBase> invalid_threads_;
  DenseMap<uptr, Tid> live_;

  void QuarantinePush(ThreadContextBase *tctx);
  ThreadContextBase *QuarantinePop();
};

typedef GenericScopedLock<ThreadRegistry> ThreadRegistryLock;

} // namespace __sanitizer

#endif // SANITIZER_THREAD_REGISTRY_H
