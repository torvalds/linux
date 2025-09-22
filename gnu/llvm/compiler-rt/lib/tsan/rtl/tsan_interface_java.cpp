//===-- tsan_interface_java.cpp -------------------------------------------===//
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

#include "tsan_interface_java.h"
#include "tsan_rtl.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_procmaps.h"

using namespace __tsan;

const jptr kHeapAlignment = 8;

namespace __tsan {

struct JavaContext {
  const uptr heap_begin;
  const uptr heap_size;

  JavaContext(jptr heap_begin, jptr heap_size)
      : heap_begin(heap_begin)
      , heap_size(heap_size) {
  }
};

static u64 jctx_buf[sizeof(JavaContext) / sizeof(u64) + 1];
static JavaContext *jctx;

MBlock *JavaHeapBlock(uptr addr, uptr *start) {
  if (!jctx || addr < jctx->heap_begin ||
      addr >= jctx->heap_begin + jctx->heap_size)
    return nullptr;
  for (uptr p = RoundDown(addr, kMetaShadowCell); p >= jctx->heap_begin;
       p -= kMetaShadowCell) {
    MBlock *b = ctx->metamap.GetBlock(p);
    if (!b)
      continue;
    if (p + b->siz <= addr)
      return nullptr;
    *start = p;
    return b;
  }
  return nullptr;
}

}  // namespace __tsan

#define JAVA_FUNC_ENTER(func)      \
  ThreadState *thr = cur_thread(); \
  (void)thr;

void __tsan_java_init(jptr heap_begin, jptr heap_size) {
  JAVA_FUNC_ENTER(__tsan_java_init);
  Initialize(thr);
  DPrintf("#%d: java_init(0x%zx, 0x%zx)\n", thr->tid, heap_begin, heap_size);
  DCHECK_EQ(jctx, 0);
  DCHECK_GT(heap_begin, 0);
  DCHECK_GT(heap_size, 0);
  DCHECK_EQ(heap_begin % kHeapAlignment, 0);
  DCHECK_EQ(heap_size % kHeapAlignment, 0);
  DCHECK_LT(heap_begin, heap_begin + heap_size);
  jctx = new(jctx_buf) JavaContext(heap_begin, heap_size);
}

int  __tsan_java_fini() {
  JAVA_FUNC_ENTER(__tsan_java_fini);
  DPrintf("#%d: java_fini()\n", thr->tid);
  DCHECK_NE(jctx, 0);
  // FIXME(dvyukov): this does not call atexit() callbacks.
  int status = Finalize(thr);
  DPrintf("#%d: java_fini() = %d\n", thr->tid, status);
  return status;
}

void __tsan_java_alloc(jptr ptr, jptr size) {
  JAVA_FUNC_ENTER(__tsan_java_alloc);
  DPrintf("#%d: java_alloc(0x%zx, 0x%zx)\n", thr->tid, ptr, size);
  DCHECK_NE(jctx, 0);
  DCHECK_NE(size, 0);
  DCHECK_EQ(ptr % kHeapAlignment, 0);
  DCHECK_EQ(size % kHeapAlignment, 0);
  DCHECK_GE(ptr, jctx->heap_begin);
  DCHECK_LE(ptr + size, jctx->heap_begin + jctx->heap_size);

  OnUserAlloc(thr, 0, ptr, size, false);
}

void __tsan_java_free(jptr ptr, jptr size) {
  JAVA_FUNC_ENTER(__tsan_java_free);
  DPrintf("#%d: java_free(0x%zx, 0x%zx)\n", thr->tid, ptr, size);
  DCHECK_NE(jctx, 0);
  DCHECK_NE(size, 0);
  DCHECK_EQ(ptr % kHeapAlignment, 0);
  DCHECK_EQ(size % kHeapAlignment, 0);
  DCHECK_GE(ptr, jctx->heap_begin);
  DCHECK_LE(ptr + size, jctx->heap_begin + jctx->heap_size);

  ctx->metamap.FreeRange(thr->proc(), ptr, size, false);
}

void __tsan_java_move(jptr src, jptr dst, jptr size) {
  JAVA_FUNC_ENTER(__tsan_java_move);
  DPrintf("#%d: java_move(0x%zx, 0x%zx, 0x%zx)\n", thr->tid, src, dst, size);
  DCHECK_NE(jctx, 0);
  DCHECK_NE(size, 0);
  DCHECK_EQ(src % kHeapAlignment, 0);
  DCHECK_EQ(dst % kHeapAlignment, 0);
  DCHECK_EQ(size % kHeapAlignment, 0);
  DCHECK_GE(src, jctx->heap_begin);
  DCHECK_LE(src + size, jctx->heap_begin + jctx->heap_size);
  DCHECK_GE(dst, jctx->heap_begin);
  DCHECK_LE(dst + size, jctx->heap_begin + jctx->heap_size);
  DCHECK_NE(dst, src);
  DCHECK_NE(size, 0);

  // Assuming it's not running concurrently with threads that do
  // memory accesses and mutex operations (stop-the-world phase).
  ctx->metamap.MoveMemory(src, dst, size);

  // Clear the destination shadow range.
  // We used to move shadow from src to dst, but the trace format does not
  // support that anymore as it contains addresses of accesses.
  RawShadow *d = MemToShadow(dst);
  RawShadow *dend = MemToShadow(dst + size);
  ShadowSet(d, dend, Shadow::kEmpty);
}

jptr __tsan_java_find(jptr *from_ptr, jptr to) {
  JAVA_FUNC_ENTER(__tsan_java_find);
  DPrintf("#%d: java_find(&0x%zx, 0x%zx)\n", thr->tid, *from_ptr, to);
  DCHECK_EQ((*from_ptr) % kHeapAlignment, 0);
  DCHECK_EQ(to % kHeapAlignment, 0);
  DCHECK_GE(*from_ptr, jctx->heap_begin);
  DCHECK_LE(to, jctx->heap_begin + jctx->heap_size);
  for (uptr from = *from_ptr; from < to; from += kHeapAlignment) {
    MBlock *b = ctx->metamap.GetBlock(from);
    if (b) {
      *from_ptr = from;
      return b->siz;
    }
  }
  return 0;
}

void __tsan_java_finalize() {
  JAVA_FUNC_ENTER(__tsan_java_finalize);
  DPrintf("#%d: java_finalize()\n", thr->tid);
  AcquireGlobal(thr);
}

void __tsan_java_mutex_lock(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_lock);
  DPrintf("#%d: java_mutex_lock(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexPostLock(thr, 0, addr,
                MutexFlagLinkerInit | MutexFlagWriteReentrant |
                    MutexFlagDoPreLockOnPostLock);
}

void __tsan_java_mutex_unlock(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_unlock);
  DPrintf("#%d: java_mutex_unlock(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexUnlock(thr, 0, addr);
}

void __tsan_java_mutex_read_lock(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_read_lock);
  DPrintf("#%d: java_mutex_read_lock(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexPostReadLock(thr, 0, addr,
                    MutexFlagLinkerInit | MutexFlagWriteReentrant |
                        MutexFlagDoPreLockOnPostLock);
}

void __tsan_java_mutex_read_unlock(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_read_unlock);
  DPrintf("#%d: java_mutex_read_unlock(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexReadUnlock(thr, 0, addr);
}

void __tsan_java_mutex_lock_rec(jptr addr, int rec) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_lock_rec);
  DPrintf("#%d: java_mutex_lock_rec(0x%zx, %d)\n", thr->tid, addr, rec);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);
  DCHECK_GT(rec, 0);

  MutexPostLock(thr, 0, addr,
                MutexFlagLinkerInit | MutexFlagWriteReentrant |
                    MutexFlagDoPreLockOnPostLock | MutexFlagRecursiveLock,
                rec);
}

int __tsan_java_mutex_unlock_rec(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_mutex_unlock_rec);
  DPrintf("#%d: java_mutex_unlock_rec(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  return MutexUnlock(thr, 0, addr, MutexFlagRecursiveUnlock);
}

void __tsan_java_acquire(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_acquire);
  DPrintf("#%d: java_acquire(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  Acquire(thr, 0, addr);
}

void __tsan_java_release(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_release);
  DPrintf("#%d: java_release(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  Release(thr, 0, addr);
}

void __tsan_java_release_store(jptr addr) {
  JAVA_FUNC_ENTER(__tsan_java_release);
  DPrintf("#%d: java_release_store(0x%zx)\n", thr->tid, addr);
  DCHECK_NE(jctx, 0);
  DCHECK_GE(addr, jctx->heap_begin);
  DCHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  ReleaseStore(thr, 0, addr);
}
