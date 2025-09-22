//===-- sanitizer_common_libcdep.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_allocator.h"
#include "sanitizer_allocator_interface.h"
#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_interface_internal.h"
#include "sanitizer_procmaps.h"
#include "sanitizer_stackdepot.h"

namespace __sanitizer {

#if (SANITIZER_LINUX || SANITIZER_NETBSD) && !SANITIZER_GO
// Weak default implementation for when sanitizer_stackdepot is not linked in.
SANITIZER_WEAK_ATTRIBUTE StackDepotStats StackDepotGetStats() { return {}; }

void *BackgroundThread(void *arg) {
  VPrintf(1, "%s: Started BackgroundThread\n", SanitizerToolName);
  const uptr hard_rss_limit_mb = common_flags()->hard_rss_limit_mb;
  const uptr soft_rss_limit_mb = common_flags()->soft_rss_limit_mb;
  const bool heap_profile = common_flags()->heap_profile;
  uptr prev_reported_rss = 0;
  uptr prev_reported_stack_depot_size = 0;
  bool reached_soft_rss_limit = false;
  uptr rss_during_last_reported_profile = 0;
  while (true) {
    SleepForMillis(100);
    const uptr current_rss_mb = GetRSS() >> 20;
    if (Verbosity()) {
      // If RSS has grown 10% since last time, print some information.
      if (prev_reported_rss * 11 / 10 < current_rss_mb) {
        Printf("%s: RSS: %zdMb\n", SanitizerToolName, current_rss_mb);
        prev_reported_rss = current_rss_mb;
      }
      // If stack depot has grown 10% since last time, print it too.
      StackDepotStats stack_depot_stats = StackDepotGetStats();
      if (prev_reported_stack_depot_size * 11 / 10 <
          stack_depot_stats.allocated) {
        Printf("%s: StackDepot: %zd ids; %zdM allocated\n", SanitizerToolName,
               stack_depot_stats.n_uniq_ids, stack_depot_stats.allocated >> 20);
        prev_reported_stack_depot_size = stack_depot_stats.allocated;
      }
    }
    // Check RSS against the limit.
    if (hard_rss_limit_mb && hard_rss_limit_mb < current_rss_mb) {
      Report("%s: hard rss limit exhausted (%zdMb vs %zdMb)\n",
             SanitizerToolName, hard_rss_limit_mb, current_rss_mb);
      DumpProcessMap();
      Die();
    }
    if (soft_rss_limit_mb) {
      if (soft_rss_limit_mb < current_rss_mb && !reached_soft_rss_limit) {
        reached_soft_rss_limit = true;
        Report("%s: soft rss limit exhausted (%zdMb vs %zdMb)\n",
               SanitizerToolName, soft_rss_limit_mb, current_rss_mb);
        SetRssLimitExceeded(true);
      } else if (soft_rss_limit_mb >= current_rss_mb &&
                 reached_soft_rss_limit) {
        reached_soft_rss_limit = false;
        Report("%s: soft rss limit unexhausted (%zdMb vs %zdMb)\n",
               SanitizerToolName, soft_rss_limit_mb, current_rss_mb);
        SetRssLimitExceeded(false);
      }
    }
    if (heap_profile &&
        current_rss_mb > rss_during_last_reported_profile * 1.1) {
      Printf("\n\nHEAP PROFILE at RSS %zdMb\n", current_rss_mb);
      __sanitizer_print_memory_profile(90, 20);
      rss_during_last_reported_profile = current_rss_mb;
    }
  }
}

void MaybeStartBackgroudThread() {
  // Need to implement/test on other platforms.
  // Start the background thread if one of the rss limits is given.
  if (!common_flags()->hard_rss_limit_mb &&
      !common_flags()->soft_rss_limit_mb &&
      !common_flags()->heap_profile) return;
  if (!&internal_pthread_create) {
    VPrintf(1, "%s: internal_pthread_create undefined\n", SanitizerToolName);
    return;  // Can't spawn the thread anyway.
  }

  static bool started = false;
  if (!started) {
    started = true;
    internal_start_thread(BackgroundThread, nullptr);
  }
}

#  if !SANITIZER_START_BACKGROUND_THREAD_IN_ASAN_INTERNAL
#    ifdef __clang__
#    pragma clang diagnostic push
// We avoid global-constructors to be sure that globals are ready when
// sanitizers need them. This can happend before global constructors executed.
// Here we don't mind if thread is started on later stages.
#    pragma clang diagnostic ignored "-Wglobal-constructors"
#    endif
static struct BackgroudThreadStarted {
  BackgroudThreadStarted() { MaybeStartBackgroudThread(); }
} background_thread_strarter UNUSED;
#    ifdef __clang__
#    pragma clang diagnostic pop
#    endif
#  endif
#else
void MaybeStartBackgroudThread() {}
#endif

void WriteToSyslog(const char *msg) {
  if (!msg)
    return;
  InternalScopedString msg_copy;
  msg_copy.Append(msg);
  const char *p = msg_copy.data();

  // Print one line at a time.
  // syslog, at least on Android, has an implicit message length limit.
  while (char* q = internal_strchr(p, '\n')) {
    *q = '\0';
    WriteOneLineToSyslog(p);
    p = q + 1;
  }
  // Print remaining characters, if there are any.
  // Note that this will add an extra newline at the end.
  // FIXME: buffer extra output. This would need a thread-local buffer, which
  // on Android requires plugging into the tools (ex. ASan's) Thread class.
  if (*p)
    WriteOneLineToSyslog(p);
}

static void (*sandboxing_callback)();
void SetSandboxingCallback(void (*f)()) {
  sandboxing_callback = f;
}

uptr ReservedAddressRange::InitAligned(uptr size, uptr align,
                                       const char *name) {
  CHECK(IsPowerOfTwo(align));
  if (align <= GetPageSizeCached())
    return Init(size, name);
  uptr start = Init(size + align, name);
  start += align - (start & (align - 1));
  return start;
}

#if !SANITIZER_FUCHSIA

// Reserve memory range [beg, end].
// We need to use inclusive range because end+1 may not be representable.
void ReserveShadowMemoryRange(uptr beg, uptr end, const char *name,
                              bool madvise_shadow) {
  CHECK_EQ((beg % GetMmapGranularity()), 0);
  CHECK_EQ(((end + 1) % GetMmapGranularity()), 0);
  uptr size = end - beg + 1;
  DecreaseTotalMmap(size);  // Don't count the shadow against mmap_limit_mb.
  if (madvise_shadow ? !MmapFixedSuperNoReserve(beg, size, name)
                     : !MmapFixedNoReserve(beg, size, name)) {
    Report(
        "ReserveShadowMemoryRange failed while trying to map 0x%zx bytes. "
        "Perhaps you're using ulimit -v or ulimit -d\n",
        size);
    Abort();
  }
  if (madvise_shadow && common_flags()->use_madv_dontdump)
    DontDumpShadowMemory(beg, size);
}

void ProtectGap(uptr addr, uptr size, uptr zero_base_shadow_start,
                uptr zero_base_max_shadow_start) {
  if (!size)
    return;
  void *res = MmapFixedNoAccess(addr, size, "shadow gap");
  if (addr == (uptr)res)
    return;
  // A few pages at the start of the address space can not be protected.
  // But we really want to protect as much as possible, to prevent this memory
  // being returned as a result of a non-FIXED mmap().
  if (addr == zero_base_shadow_start) {
    uptr step = GetMmapGranularity();
    while (size > step && addr < zero_base_max_shadow_start) {
      addr += step;
      size -= step;
      void *res = MmapFixedNoAccess(addr, size, "shadow gap");
      if (addr == (uptr)res)
        return;
    }
  }

  Report(
      "ERROR: Failed to protect the shadow gap. "
      "%s cannot proceed correctly. ABORTING.\n",
      SanitizerToolName);
  DumpProcessMap();
  Die();
}

#endif  // !SANITIZER_FUCHSIA

#if !SANITIZER_WINDOWS && !SANITIZER_GO
// Weak default implementation for when sanitizer_stackdepot is not linked in.
SANITIZER_WEAK_ATTRIBUTE void StackDepotStopBackgroundThread() {}
static void StopStackDepotBackgroundThread() {
  StackDepotStopBackgroundThread();
}
#else
// SANITIZER_WEAK_ATTRIBUTE is unsupported.
static void StopStackDepotBackgroundThread() {}
#endif

}  // namespace __sanitizer

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_sandbox_on_notify,
                             __sanitizer_sandbox_arguments *args) {
  __sanitizer::StopStackDepotBackgroundThread();
  __sanitizer::PlatformPrepareForSandboxing(args);
  if (__sanitizer::sandboxing_callback)
    __sanitizer::sandboxing_callback();
}
