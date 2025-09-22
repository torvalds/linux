//===-- tsan_mman.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_MMAN_H
#define TSAN_MMAN_H

#include "tsan_defs.h"

namespace __tsan {

const uptr kDefaultAlignment = 16;

void InitializeAllocator();
void InitializeAllocatorLate();
void ReplaceSystemMalloc();
void AllocatorProcStart(Processor *proc);
void AllocatorProcFinish(Processor *proc);
void AllocatorPrintStats();
void AllocatorLockBeforeFork();
void AllocatorUnlockAfterFork(bool child);
void GlobalProcessorLock();
void GlobalProcessorUnlock();

// For user allocations.
void *user_alloc_internal(ThreadState *thr, uptr pc, uptr sz,
                          uptr align = kDefaultAlignment, bool signal = true);
// Does not accept NULL.
void user_free(ThreadState *thr, uptr pc, void *p, bool signal = true);
// Interceptor implementations.
void *user_alloc(ThreadState *thr, uptr pc, uptr sz);
void *user_calloc(ThreadState *thr, uptr pc, uptr sz, uptr n);
void *user_realloc(ThreadState *thr, uptr pc, void *p, uptr sz);
void *user_reallocarray(ThreadState *thr, uptr pc, void *p, uptr sz, uptr n);
void *user_memalign(ThreadState *thr, uptr pc, uptr align, uptr sz);
int user_posix_memalign(ThreadState *thr, uptr pc, void **memptr, uptr align,
                        uptr sz);
void *user_aligned_alloc(ThreadState *thr, uptr pc, uptr align, uptr sz);
void *user_valloc(ThreadState *thr, uptr pc, uptr sz);
void *user_pvalloc(ThreadState *thr, uptr pc, uptr sz);
uptr user_alloc_usable_size(const void *p);

// Invoking malloc/free hooks that may be installed by the user.
void invoke_malloc_hook(void *ptr, uptr size);
void invoke_free_hook(void *ptr);

// For internal data structures.
void *Alloc(uptr sz);
void FreeImpl(void *p);

template <typename T, typename... Args>
T *New(Args &&...args) {
  return new (Alloc(sizeof(T))) T(static_cast<Args &&>(args)...);
}

template <typename T>
void Free(T *&p) {
  if (p == nullptr)
    return;
  FreeImpl(p);
  p = nullptr;
}

template <typename T>
void DestroyAndFree(T *&p) {
  if (p == nullptr)
    return;
  p->~T();
  Free(p);
}

}  // namespace __tsan
#endif  // TSAN_MMAN_H
