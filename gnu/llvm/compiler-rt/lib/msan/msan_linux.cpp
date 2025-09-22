//===-- msan_linux.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Linux-, NetBSD- and FreeBSD-specific code.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD

#  include <elf.h>
#  include <link.h>
#  include <pthread.h>
#  include <signal.h>
#  include <stdio.h>
#  include <stdlib.h>
#  if SANITIZER_LINUX
#    include <sys/personality.h>
#  endif
#  include <sys/resource.h>
#  include <sys/time.h>
#  include <unistd.h>
#  include <unwind.h>

#  include "msan.h"
#  include "msan_allocator.h"
#  include "msan_chained_origin_depot.h"
#  include "msan_report.h"
#  include "msan_thread.h"
#  include "sanitizer_common/sanitizer_common.h"
#  include "sanitizer_common/sanitizer_procmaps.h"
#  include "sanitizer_common/sanitizer_stackdepot.h"

namespace __msan {

void ReportMapRange(const char *descr, uptr beg, uptr size) {
  if (size > 0) {
    uptr end = beg + size - 1;
    VPrintf(1, "%s : %p-%p\n", descr, (void *)beg, (void *)end);
  }
}

static bool CheckMemoryRangeAvailability(uptr beg, uptr size, bool verbose) {
  if (size > 0) {
    uptr end = beg + size - 1;
    if (!MemoryRangeIsAvailable(beg, end)) {
      if (verbose)
        Printf("FATAL: MemorySanitizer: Shadow range %p-%p is not available.\n",
               (void *)beg, (void *)end);
      return false;
    }
  }
  return true;
}

static bool ProtectMemoryRange(uptr beg, uptr size, const char *name) {
  if (size > 0) {
    void *addr = MmapFixedNoAccess(beg, size, name);
    if (beg == 0 && addr) {
      // Depending on the kernel configuration, we may not be able to protect
      // the page at address zero.
      uptr gap = 16 * GetPageSizeCached();
      beg += gap;
      size -= gap;
      addr = MmapFixedNoAccess(beg, size, name);
    }
    if ((uptr)addr != beg) {
      uptr end = beg + size - 1;
      Printf(
          "FATAL: MemorySanitizer: Cannot protect memory range %p-%p (%s).\n",
          (void *)beg, (void *)end, name);
      return false;
    }
  }
  return true;
}

static void CheckMemoryLayoutSanity() {
  uptr prev_end = 0;
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i) {
    uptr start = kMemoryLayout[i].start;
    uptr end = kMemoryLayout[i].end;
    MappingDesc::Type type = kMemoryLayout[i].type;
    CHECK_LT(start, end);
    CHECK_EQ(prev_end, start);
    CHECK(addr_is_type(start, type));
    CHECK(addr_is_type((start + end) / 2, type));
    CHECK(addr_is_type(end - 1, type));
    if (type == MappingDesc::APP || type == MappingDesc::ALLOCATOR) {
      uptr addr = start;
      CHECK(MEM_IS_SHADOW(MEM_TO_SHADOW(addr)));
      CHECK(MEM_IS_ORIGIN(MEM_TO_ORIGIN(addr)));
      CHECK_EQ(MEM_TO_ORIGIN(addr), SHADOW_TO_ORIGIN(MEM_TO_SHADOW(addr)));

      addr = (start + end) / 2;
      CHECK(MEM_IS_SHADOW(MEM_TO_SHADOW(addr)));
      CHECK(MEM_IS_ORIGIN(MEM_TO_ORIGIN(addr)));
      CHECK_EQ(MEM_TO_ORIGIN(addr), SHADOW_TO_ORIGIN(MEM_TO_SHADOW(addr)));

      addr = end - 1;
      CHECK(MEM_IS_SHADOW(MEM_TO_SHADOW(addr)));
      CHECK(MEM_IS_ORIGIN(MEM_TO_ORIGIN(addr)));
      CHECK_EQ(MEM_TO_ORIGIN(addr), SHADOW_TO_ORIGIN(MEM_TO_SHADOW(addr)));
    }
    prev_end = end;
  }
}

static bool InitShadow(bool init_origins, bool dry_run) {
  // Let user know mapping parameters first.
  VPrintf(1, "__msan_init %p\n", reinterpret_cast<void *>(&__msan_init));
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i)
    VPrintf(1, "%s: %zx - %zx\n", kMemoryLayout[i].name, kMemoryLayout[i].start,
            kMemoryLayout[i].end - 1);

  CheckMemoryLayoutSanity();

  if (!MEM_IS_APP(&__msan_init)) {
    if (!dry_run)
      Printf("FATAL: Code %p is out of application range. Non-PIE build?\n",
             reinterpret_cast<void *>(&__msan_init));
    return false;
  }

  const uptr maxVirtualAddress = GetMaxUserVirtualAddress();

  for (unsigned i = 0; i < kMemoryLayoutSize; ++i) {
    uptr start = kMemoryLayout[i].start;
    uptr end = kMemoryLayout[i].end;
    uptr size = end - start;
    MappingDesc::Type type = kMemoryLayout[i].type;

    // Check if the segment should be mapped based on platform constraints.
    if (start >= maxVirtualAddress)
      continue;

    bool map = type == MappingDesc::SHADOW ||
               (init_origins && type == MappingDesc::ORIGIN);
    bool protect = type == MappingDesc::INVALID ||
                   (!init_origins && type == MappingDesc::ORIGIN);
    CHECK(!(map && protect));
    if (!map && !protect) {
      CHECK(type == MappingDesc::APP || type == MappingDesc::ALLOCATOR);

      if (dry_run && type == MappingDesc::ALLOCATOR &&
          !CheckMemoryRangeAvailability(start, size, !dry_run))
        return false;
    }
    if (map) {
      if (dry_run && !CheckMemoryRangeAvailability(start, size, !dry_run))
        return false;
      if (!dry_run &&
          !MmapFixedSuperNoReserve(start, size, kMemoryLayout[i].name))
        return false;
      if (!dry_run && common_flags()->use_madv_dontdump)
        DontDumpShadowMemory(start, size);
    }
    if (protect) {
      if (dry_run && !CheckMemoryRangeAvailability(start, size, !dry_run))
        return false;
      if (!dry_run && !ProtectMemoryRange(start, size, kMemoryLayout[i].name))
        return false;
    }
  }

  return true;
}

bool InitShadowWithReExec(bool init_origins) {
  // Start with dry run: check layout is ok, but don't print warnings because
  // warning messages will cause tests to fail (even if we successfully re-exec
  // after the warning).
  bool success = InitShadow(init_origins, true);
  if (!success) {
#  if SANITIZER_LINUX
    // Perhaps ASLR entropy is too high. If ASLR is enabled, re-exec without it.
    int old_personality = personality(0xffffffff);
    bool aslr_on =
        (old_personality != -1) && ((old_personality & ADDR_NO_RANDOMIZE) == 0);

    if (aslr_on) {
      VReport(1,
              "WARNING: MemorySanitizer: memory layout is incompatible, "
              "possibly due to high-entropy ASLR.\n"
              "Re-execing with fixed virtual address space.\n"
              "N.B. reducing ASLR entropy is preferable.\n");
      CHECK_NE(personality(old_personality | ADDR_NO_RANDOMIZE), -1);
      ReExec();
    }
#  endif
  }

  // The earlier dry run didn't actually map or protect anything. Run again in
  // non-dry run mode.
  return success && InitShadow(init_origins, false);
}

static void MsanAtExit(void) {
  if (flags()->print_stats && (flags()->atexit || msan_report_count > 0))
    ReportStats();
  if (msan_report_count > 0) {
    ReportAtExitStatistics();
    if (common_flags()->exitcode)
      internal__exit(common_flags()->exitcode);
  }
}

void InstallAtExitHandler() {
  atexit(MsanAtExit);
}

// ---------------------- TSD ---------------- {{{1

#if SANITIZER_NETBSD
// Thread Static Data cannot be used in early init on NetBSD.
// Reuse the MSan TSD API for compatibility with existing code
// with an alternative implementation.

static void (*tsd_destructor)(void *tsd) = nullptr;

struct tsd_key {
  tsd_key() : key(nullptr) {}
  ~tsd_key() {
    CHECK(tsd_destructor);
    if (key)
      (*tsd_destructor)(key);
  }
  MsanThread *key;
};

static thread_local struct tsd_key key;

void MsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_destructor);
  tsd_destructor = destructor;
}

MsanThread *GetCurrentThread() {
  CHECK(tsd_destructor);
  return key.key;
}

void SetCurrentThread(MsanThread *tsd) {
  CHECK(tsd_destructor);
  CHECK(tsd);
  CHECK(!key.key);
  key.key = tsd;
}

void MsanTSDDtor(void *tsd) {
  CHECK(tsd_destructor);
  CHECK_EQ(key.key, tsd);
  key.key = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  MsanThread::TSDDtor(tsd);
}
#else
static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void MsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

static THREADLOCAL MsanThread* msan_current_thread;

MsanThread *GetCurrentThread() {
  return msan_current_thread;
}

void SetCurrentThread(MsanThread *t) {
  // Make sure we do not reset the current MsanThread.
  CHECK_EQ(0, msan_current_thread);
  msan_current_thread = t;
  // Make sure that MsanTSDDtor gets called at the end.
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, (void *)t);
}

void MsanTSDDtor(void *tsd) {
  MsanThread *t = (MsanThread*)tsd;
  if (t->destructor_iterations_ > 1) {
    t->destructor_iterations_--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
  ScopedBlockSignals block(nullptr);
  msan_current_thread = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  MsanThread::TSDDtor(tsd);
}
#  endif

static void BeforeFork() {
  // Usually we lock ThreadRegistry, but msan does not have one.
  LockAllocator();
  StackDepotLockBeforeFork();
  ChainedOriginDepotBeforeFork();
}

static void AfterFork(bool fork_child) {
  ChainedOriginDepotAfterFork(fork_child);
  StackDepotUnlockAfterFork(fork_child);
  UnlockAllocator();
  // Usually we unlock ThreadRegistry, but msan does not have one.
}

void InstallAtForkHandler() {
  pthread_atfork(
      &BeforeFork, []() { AfterFork(/* fork_child= */ false); },
      []() { AfterFork(/* fork_child= */ true); });
}

} // namespace __msan

#endif // SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD
