//===-- memprof_thread.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf-private header for memprof_thread.cpp.
//===----------------------------------------------------------------------===//

#ifndef MEMPROF_THREAD_H
#define MEMPROF_THREAD_H

#include "memprof_allocator.h"
#include "memprof_internal.h"
#include "memprof_stats.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_thread_registry.h"

namespace __sanitizer {
struct DTLS;
} // namespace __sanitizer

namespace __memprof {

class MemprofThread;

// These objects are created for every thread and are never deleted,
// so we can find them by tid even if the thread is long dead.
struct MemprofThreadContext final : public ThreadContextBase {
  explicit MemprofThreadContext(int tid)
      : ThreadContextBase(tid), announced(false),
        destructor_iterations(GetPthreadDestructorIterations()), stack_id(0),
        thread(nullptr) {}
  bool announced;
  u8 destructor_iterations;
  u32 stack_id;
  MemprofThread *thread;

  void OnCreated(void *arg) override;
  void OnFinished() override;

  struct CreateThreadContextArgs {
    MemprofThread *thread;
    StackTrace *stack;
  };
};

// MemprofThreadContext objects are never freed, so we need many of them.
COMPILER_CHECK(sizeof(MemprofThreadContext) <= 256);

// MemprofThread are stored in TSD and destroyed when the thread dies.
class MemprofThread {
public:
  static MemprofThread *Create(thread_callback_t start_routine, void *arg,
                               u32 parent_tid, StackTrace *stack,
                               bool detached);
  static void TSDDtor(void *tsd);
  void Destroy();

  struct InitOptions;
  void Init(const InitOptions *options = nullptr);

  thread_return_t ThreadStart(tid_t os_id,
                              atomic_uintptr_t *signal_thread_is_registered);

  uptr stack_top();
  uptr stack_bottom();
  uptr stack_size();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }
  u32 tid() { return context_->tid; }
  MemprofThreadContext *context() { return context_; }
  void set_context(MemprofThreadContext *context) { context_ = context; }

  bool AddrIsInStack(uptr addr);

  // True is this thread is currently unwinding stack (i.e. collecting a stack
  // trace). Used to prevent deadlocks on platforms where libc unwinder calls
  // malloc internally. See PR17116 for more details.
  bool isUnwinding() const { return unwinding_; }
  void setUnwinding(bool b) { unwinding_ = b; }

  MemprofThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }
  MemprofStats &stats() { return stats_; }

private:
  // NOTE: There is no MemprofThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.

  void SetThreadStackAndTls(const InitOptions *options);

  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;

  MemprofThreadContext *context_;
  thread_callback_t start_routine_;
  void *arg_;

  uptr stack_top_;
  uptr stack_bottom_;

  uptr tls_begin_;
  uptr tls_end_;
  DTLS *dtls_;

  MemprofThreadLocalMallocStorage malloc_storage_;
  MemprofStats stats_;
  bool unwinding_;
};

// Returns a single instance of registry.
ThreadRegistry &memprofThreadRegistry();

// Must be called under ThreadRegistryLock.
MemprofThreadContext *GetThreadContextByTidLocked(u32 tid);

// Get the current thread. May return 0.
MemprofThread *GetCurrentThread();
void SetCurrentThread(MemprofThread *t);
u32 GetCurrentTidOrInvalid();

// Used to handle fork().
void EnsureMainThreadIDIsCorrect();
} // namespace __memprof

#endif // MEMPROF_THREAD_H
