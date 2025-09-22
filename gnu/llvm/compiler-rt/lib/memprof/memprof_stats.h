//===-- memprof_stats.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf-private header for statistics.
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_STATS_H
#define MEMPROF_STATS_H

#include "memprof_allocator.h"
#include "memprof_internal.h"

namespace __memprof {

// MemprofStats struct is NOT thread-safe.
// Each MemprofThread has its own MemprofStats, which are sometimes flushed
// to the accumulated MemprofStats.
struct MemprofStats {
  // MemprofStats must be a struct consisting of uptr fields only.
  // When merging two MemprofStats structs, we treat them as arrays of uptr.
  uptr mallocs;
  uptr malloced;
  uptr malloced_overhead;
  uptr frees;
  uptr freed;
  uptr real_frees;
  uptr really_freed;
  uptr reallocs;
  uptr realloced;
  uptr mmaps;
  uptr mmaped;
  uptr munmaps;
  uptr munmaped;
  uptr malloc_large;
  uptr malloced_by_size[kNumberOfSizeClasses];

  // Ctor for global MemprofStats (accumulated stats for dead threads).
  explicit MemprofStats(LinkerInitialized) {}
  // Creates empty stats.
  MemprofStats();

  void Print(); // Prints formatted stats to stderr.
  void Clear();
  void MergeFrom(const MemprofStats *stats);
};

// Returns stats for GetCurrentThread(), or stats for fake "unknown thread"
// if GetCurrentThread() returns 0.
MemprofStats &GetCurrentThreadStats();
// Flushes a given stats into accumulated stats of dead threads.
void FlushToDeadThreadStats(MemprofStats *stats);

} // namespace __memprof

#endif // MEMPROF_STATS_H
