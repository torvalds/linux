//===-- memprof_stats.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// Code related to statistics collected by MemProfiler.
//===----------------------------------------------------------------------===//
#include "memprof_stats.h"
#include "memprof_interceptors.h"
#include "memprof_internal.h"
#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __memprof {

MemprofStats::MemprofStats() { Clear(); }

void MemprofStats::Clear() {
  if (REAL(memset))
    return (void)REAL(memset)(this, 0, sizeof(MemprofStats));
  internal_memset(this, 0, sizeof(MemprofStats));
}

static void PrintMallocStatsArray(const char *prefix,
                                  uptr (&array)[kNumberOfSizeClasses]) {
  Printf("%s", prefix);
  for (uptr i = 0; i < kNumberOfSizeClasses; i++) {
    if (!array[i])
      continue;
    Printf("%zu:%zu; ", i, array[i]);
  }
  Printf("\n");
}

void MemprofStats::Print() {
  Printf("Stats: %zuM malloced (%zuM for overhead) by %zu calls\n",
         malloced >> 20, malloced_overhead >> 20, mallocs);
  Printf("Stats: %zuM realloced by %zu calls\n", realloced >> 20, reallocs);
  Printf("Stats: %zuM freed by %zu calls\n", freed >> 20, frees);
  Printf("Stats: %zuM really freed by %zu calls\n", really_freed >> 20,
         real_frees);
  Printf("Stats: %zuM (%zuM-%zuM) mmaped; %zu maps, %zu unmaps\n",
         (mmaped - munmaped) >> 20, mmaped >> 20, munmaped >> 20, mmaps,
         munmaps);

  PrintMallocStatsArray("  mallocs by size class: ", malloced_by_size);
  Printf("Stats: malloc large: %zu\n", malloc_large);
}

void MemprofStats::MergeFrom(const MemprofStats *stats) {
  uptr *dst_ptr = reinterpret_cast<uptr *>(this);
  const uptr *src_ptr = reinterpret_cast<const uptr *>(stats);
  uptr num_fields = sizeof(*this) / sizeof(uptr);
  for (uptr i = 0; i < num_fields; i++)
    dst_ptr[i] += src_ptr[i];
}

static Mutex print_lock;

static MemprofStats unknown_thread_stats(LINKER_INITIALIZED);
static MemprofStats dead_threads_stats(LINKER_INITIALIZED);
static Mutex dead_threads_stats_lock;
// Required for malloc_zone_statistics() on OS X. This can't be stored in
// per-thread MemprofStats.
static uptr max_malloced_memory;

static void MergeThreadStats(ThreadContextBase *tctx_base, void *arg) {
  MemprofStats *accumulated_stats = reinterpret_cast<MemprofStats *>(arg);
  MemprofThreadContext *tctx = static_cast<MemprofThreadContext *>(tctx_base);
  if (MemprofThread *t = tctx->thread)
    accumulated_stats->MergeFrom(&t->stats());
}

static void GetAccumulatedStats(MemprofStats *stats) {
  stats->Clear();
  {
    ThreadRegistryLock l(&memprofThreadRegistry());
    memprofThreadRegistry().RunCallbackForEachThreadLocked(MergeThreadStats,
                                                           stats);
  }
  stats->MergeFrom(&unknown_thread_stats);
  {
    Lock lock(&dead_threads_stats_lock);
    stats->MergeFrom(&dead_threads_stats);
  }
  // This is not very accurate: we may miss allocation peaks that happen
  // between two updates of accumulated_stats_. For more accurate bookkeeping
  // the maximum should be updated on every malloc(), which is unacceptable.
  if (max_malloced_memory < stats->malloced) {
    max_malloced_memory = stats->malloced;
  }
}

void FlushToDeadThreadStats(MemprofStats *stats) {
  Lock lock(&dead_threads_stats_lock);
  dead_threads_stats.MergeFrom(stats);
  stats->Clear();
}

MemprofStats &GetCurrentThreadStats() {
  MemprofThread *t = GetCurrentThread();
  return (t) ? t->stats() : unknown_thread_stats;
}

static void PrintAccumulatedStats() {
  MemprofStats stats;
  GetAccumulatedStats(&stats);
  // Use lock to keep reports from mixing up.
  Lock lock(&print_lock);
  stats.Print();
  StackDepotStats stack_depot_stats = StackDepotGetStats();
  Printf("Stats: StackDepot: %zd ids; %zdM allocated\n",
         stack_depot_stats.n_uniq_ids, stack_depot_stats.allocated >> 20);
  PrintInternalAllocatorStats();
}

} // namespace __memprof

// ---------------------- Interface ---------------- {{{1
using namespace __memprof;

uptr __sanitizer_get_current_allocated_bytes() {
  MemprofStats stats;
  GetAccumulatedStats(&stats);
  uptr malloced = stats.malloced;
  uptr freed = stats.freed;
  // Return sane value if malloced < freed due to racy
  // way we update accumulated stats.
  return (malloced > freed) ? malloced - freed : 1;
}

uptr __sanitizer_get_heap_size() {
  MemprofStats stats;
  GetAccumulatedStats(&stats);
  return stats.mmaped - stats.munmaped;
}

uptr __sanitizer_get_free_bytes() {
  MemprofStats stats;
  GetAccumulatedStats(&stats);
  uptr total_free = stats.mmaped - stats.munmaped + stats.really_freed;
  uptr total_used = stats.malloced;
  // Return sane value if total_free < total_used due to racy
  // way we update accumulated stats.
  return (total_free > total_used) ? total_free - total_used : 1;
}

uptr __sanitizer_get_unmapped_bytes() { return 0; }

void __memprof_print_accumulated_stats() { PrintAccumulatedStats(); }
