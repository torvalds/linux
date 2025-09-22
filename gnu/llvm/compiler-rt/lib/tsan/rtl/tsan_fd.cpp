//===-- tsan_fd.cpp -------------------------------------------------------===//
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

#include "tsan_fd.h"

#include <sanitizer_common/sanitizer_atomic.h>

#include "tsan_interceptors.h"
#include "tsan_rtl.h"

namespace __tsan {

const int kTableSizeL1 = 1024;
const int kTableSizeL2 = 1024;
const int kTableSize = kTableSizeL1 * kTableSizeL2;

struct FdSync {
  atomic_uint64_t rc;
};

struct FdDesc {
  FdSync *sync;
  // This is used to establish write -> epoll_wait synchronization
  // where epoll_wait receives notification about the write.
  atomic_uintptr_t aux_sync;  // FdSync*
  Tid creation_tid;
  StackID creation_stack;
  bool closed;
};

struct FdContext {
  atomic_uintptr_t tab[kTableSizeL1];
  // Addresses used for synchronization.
  FdSync globsync;
  FdSync filesync;
  FdSync socksync;
  u64 connectsync;
};

static FdContext fdctx;

static bool bogusfd(int fd) {
  // Apparently a bogus fd value.
  return fd < 0 || fd >= kTableSize;
}

static FdSync *allocsync(ThreadState *thr, uptr pc) {
  FdSync *s = (FdSync*)user_alloc_internal(thr, pc, sizeof(FdSync),
      kDefaultAlignment, false);
  atomic_store(&s->rc, 1, memory_order_relaxed);
  return s;
}

static FdSync *ref(FdSync *s) {
  if (s && atomic_load(&s->rc, memory_order_relaxed) != (u64)-1)
    atomic_fetch_add(&s->rc, 1, memory_order_relaxed);
  return s;
}

static void unref(ThreadState *thr, uptr pc, FdSync *s) {
  if (s && atomic_load(&s->rc, memory_order_relaxed) != (u64)-1) {
    if (atomic_fetch_sub(&s->rc, 1, memory_order_acq_rel) == 1) {
      CHECK_NE(s, &fdctx.globsync);
      CHECK_NE(s, &fdctx.filesync);
      CHECK_NE(s, &fdctx.socksync);
      user_free(thr, pc, s, false);
    }
  }
}

static FdDesc *fddesc(ThreadState *thr, uptr pc, int fd) {
  CHECK_GE(fd, 0);
  CHECK_LT(fd, kTableSize);
  atomic_uintptr_t *pl1 = &fdctx.tab[fd / kTableSizeL2];
  uptr l1 = atomic_load(pl1, memory_order_consume);
  if (l1 == 0) {
    uptr size = kTableSizeL2 * sizeof(FdDesc);
    // We need this to reside in user memory to properly catch races on it.
    void *p = user_alloc_internal(thr, pc, size, kDefaultAlignment, false);
    internal_memset(p, 0, size);
    MemoryResetRange(thr, (uptr)&fddesc, (uptr)p, size);
    if (atomic_compare_exchange_strong(pl1, &l1, (uptr)p, memory_order_acq_rel))
      l1 = (uptr)p;
    else
      user_free(thr, pc, p, false);
  }
  FdDesc *fds = reinterpret_cast<FdDesc *>(l1);
  return &fds[fd % kTableSizeL2];
}

// pd must be already ref'ed.
static void init(ThreadState *thr, uptr pc, int fd, FdSync *s,
    bool write = true) {
  FdDesc *d = fddesc(thr, pc, fd);
  // As a matter of fact, we don't intercept all close calls.
  // See e.g. libc __res_iclose().
  if (d->sync) {
    unref(thr, pc, d->sync);
    d->sync = 0;
  }
  unref(thr, pc,
        reinterpret_cast<FdSync *>(
            atomic_load(&d->aux_sync, memory_order_relaxed)));
  atomic_store(&d->aux_sync, 0, memory_order_relaxed);
  if (flags()->io_sync == 0) {
    unref(thr, pc, s);
  } else if (flags()->io_sync == 1) {
    d->sync = s;
  } else if (flags()->io_sync == 2) {
    unref(thr, pc, s);
    d->sync = &fdctx.globsync;
  }
  d->creation_tid = thr->tid;
  d->creation_stack = CurrentStackId(thr, pc);
  d->closed = false;
  // This prevents false positives on fd_close_norace3.cpp test.
  // The mechanics of the false positive are not completely clear,
  // but it happens only if global reset is enabled (flush_memory_ms=1)
  // and may be related to lost writes during asynchronous MADV_DONTNEED.
  SlotLocker locker(thr);
  if (write) {
    // To catch races between fd usage and open.
    MemoryRangeImitateWrite(thr, pc, (uptr)d, 8);
  } else {
    // See the dup-related comment in FdClose.
    MemoryAccess(thr, pc, (uptr)d, 8, kAccessRead | kAccessSlotLocked);
  }
}

void FdInit() {
  atomic_store(&fdctx.globsync.rc, (u64)-1, memory_order_relaxed);
  atomic_store(&fdctx.filesync.rc, (u64)-1, memory_order_relaxed);
  atomic_store(&fdctx.socksync.rc, (u64)-1, memory_order_relaxed);
}

void FdOnFork(ThreadState *thr, uptr pc) {
  // On fork() we need to reset all fd's, because the child is going
  // close all them, and that will cause races between previous read/write
  // and the close.
  for (int l1 = 0; l1 < kTableSizeL1; l1++) {
    FdDesc *tab = (FdDesc*)atomic_load(&fdctx.tab[l1], memory_order_relaxed);
    if (tab == 0)
      break;
    for (int l2 = 0; l2 < kTableSizeL2; l2++) {
      FdDesc *d = &tab[l2];
      MemoryResetRange(thr, pc, (uptr)d, 8);
    }
  }
}

bool FdLocation(uptr addr, int *fd, Tid *tid, StackID *stack, bool *closed) {
  for (int l1 = 0; l1 < kTableSizeL1; l1++) {
    FdDesc *tab = (FdDesc*)atomic_load(&fdctx.tab[l1], memory_order_relaxed);
    if (tab == 0)
      break;
    if (addr >= (uptr)tab && addr < (uptr)(tab + kTableSizeL2)) {
      int l2 = (addr - (uptr)tab) / sizeof(FdDesc);
      FdDesc *d = &tab[l2];
      *fd = l1 * kTableSizeL1 + l2;
      *tid = d->creation_tid;
      *stack = d->creation_stack;
      *closed = d->closed;
      return true;
    }
  }
  return false;
}

void FdAcquire(ThreadState *thr, uptr pc, int fd) {
  if (bogusfd(fd))
    return;
  FdDesc *d = fddesc(thr, pc, fd);
  FdSync *s = d->sync;
  DPrintf("#%d: FdAcquire(%d) -> %p\n", thr->tid, fd, s);
  MemoryAccess(thr, pc, (uptr)d, 8, kAccessRead);
  if (s)
    Acquire(thr, pc, (uptr)s);
}

void FdRelease(ThreadState *thr, uptr pc, int fd) {
  if (bogusfd(fd))
    return;
  FdDesc *d = fddesc(thr, pc, fd);
  FdSync *s = d->sync;
  DPrintf("#%d: FdRelease(%d) -> %p\n", thr->tid, fd, s);
  MemoryAccess(thr, pc, (uptr)d, 8, kAccessRead);
  if (s)
    Release(thr, pc, (uptr)s);
  if (uptr aux_sync = atomic_load(&d->aux_sync, memory_order_acquire))
    Release(thr, pc, aux_sync);
}

void FdAccess(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdAccess(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  FdDesc *d = fddesc(thr, pc, fd);
  MemoryAccess(thr, pc, (uptr)d, 8, kAccessRead);
}

void FdClose(ThreadState *thr, uptr pc, int fd, bool write) {
  DPrintf("#%d: FdClose(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  FdDesc *d = fddesc(thr, pc, fd);
  {
    // Need to lock the slot to make MemoryAccess and MemoryResetRange atomic
    // with respect to global reset. See the comment in MemoryRangeFreed.
    SlotLocker locker(thr);
    if (!MustIgnoreInterceptor(thr)) {
      if (write) {
        // To catch races between fd usage and close.
        MemoryAccess(thr, pc, (uptr)d, 8,
                     kAccessWrite | kAccessCheckOnly | kAccessSlotLocked);
      } else {
        // This path is used only by dup2/dup3 calls.
        // We do read instead of write because there is a number of legitimate
        // cases where write would lead to false positives:
        // 1. Some software dups a closed pipe in place of a socket before
        // closing
        //    the socket (to prevent races actually).
        // 2. Some daemons dup /dev/null in place of stdin/stdout.
        // On the other hand we have not seen cases when write here catches real
        // bugs.
        MemoryAccess(thr, pc, (uptr)d, 8,
                     kAccessRead | kAccessCheckOnly | kAccessSlotLocked);
      }
    }
    // We need to clear it, because if we do not intercept any call out there
    // that creates fd, we will hit false postives.
    MemoryResetRange(thr, pc, (uptr)d, 8);
  }
  unref(thr, pc, d->sync);
  d->sync = 0;
  unref(thr, pc,
        reinterpret_cast<FdSync *>(
            atomic_load(&d->aux_sync, memory_order_relaxed)));
  atomic_store(&d->aux_sync, 0, memory_order_relaxed);
  d->closed = true;
  d->creation_tid = thr->tid;
  d->creation_stack = CurrentStackId(thr, pc);
}

void FdFileCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdFileCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, &fdctx.filesync);
}

void FdDup(ThreadState *thr, uptr pc, int oldfd, int newfd, bool write) {
  DPrintf("#%d: FdDup(%d, %d)\n", thr->tid, oldfd, newfd);
  if (bogusfd(oldfd) || bogusfd(newfd))
    return;
  // Ignore the case when user dups not yet connected socket.
  FdDesc *od = fddesc(thr, pc, oldfd);
  MemoryAccess(thr, pc, (uptr)od, 8, kAccessRead);
  FdClose(thr, pc, newfd, write);
  init(thr, pc, newfd, ref(od->sync), write);
}

void FdPipeCreate(ThreadState *thr, uptr pc, int rfd, int wfd) {
  DPrintf("#%d: FdCreatePipe(%d, %d)\n", thr->tid, rfd, wfd);
  FdSync *s = allocsync(thr, pc);
  init(thr, pc, rfd, ref(s));
  init(thr, pc, wfd, ref(s));
  unref(thr, pc, s);
}

void FdEventCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdEventCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, allocsync(thr, pc));
}

void FdSignalCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdSignalCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, 0);
}

void FdInotifyCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdInotifyCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, 0);
}

void FdPollCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdPollCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, allocsync(thr, pc));
}

void FdPollAdd(ThreadState *thr, uptr pc, int epfd, int fd) {
  DPrintf("#%d: FdPollAdd(%d, %d)\n", thr->tid, epfd, fd);
  if (bogusfd(epfd) || bogusfd(fd))
    return;
  FdDesc *d = fddesc(thr, pc, fd);
  // Associate fd with epoll fd only once.
  // While an fd can be associated with multiple epolls at the same time,
  // or with different epolls during different phases of lifetime,
  // synchronization semantics (and examples) of this are unclear.
  // So we don't support this for now.
  // If we change the association, it will also create lifetime management
  // problem for FdRelease which accesses the aux_sync.
  if (atomic_load(&d->aux_sync, memory_order_relaxed))
    return;
  FdDesc *epd = fddesc(thr, pc, epfd);
  FdSync *s = epd->sync;
  if (!s)
    return;
  uptr cmp = 0;
  if (atomic_compare_exchange_strong(
          &d->aux_sync, &cmp, reinterpret_cast<uptr>(s), memory_order_release))
    ref(s);
}

void FdSocketCreate(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdSocketCreate(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  // It can be a UDP socket.
  init(thr, pc, fd, &fdctx.socksync);
}

void FdSocketAccept(ThreadState *thr, uptr pc, int fd, int newfd) {
  DPrintf("#%d: FdSocketAccept(%d, %d)\n", thr->tid, fd, newfd);
  if (bogusfd(fd))
    return;
  // Synchronize connect->accept.
  Acquire(thr, pc, (uptr)&fdctx.connectsync);
  init(thr, pc, newfd, &fdctx.socksync);
}

void FdSocketConnecting(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdSocketConnecting(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  // Synchronize connect->accept.
  Release(thr, pc, (uptr)&fdctx.connectsync);
}

void FdSocketConnect(ThreadState *thr, uptr pc, int fd) {
  DPrintf("#%d: FdSocketConnect(%d)\n", thr->tid, fd);
  if (bogusfd(fd))
    return;
  init(thr, pc, fd, &fdctx.socksync);
}

uptr File2addr(const char *path) {
  (void)path;
  static u64 addr;
  return (uptr)&addr;
}

uptr Dir2addr(const char *path) {
  (void)path;
  static u64 addr;
  return (uptr)&addr;
}

}  //  namespace __tsan
