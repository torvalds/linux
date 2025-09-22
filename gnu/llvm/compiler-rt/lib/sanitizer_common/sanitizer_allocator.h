//===-- sanitizer_allocator.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Specialized memory allocator for ThreadSanitizer, MemorySanitizer, etc.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALLOCATOR_H
#define SANITIZER_ALLOCATOR_H

#include "sanitizer_common.h"
#include "sanitizer_flat_map.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_lfstack.h"
#include "sanitizer_libc.h"
#include "sanitizer_list.h"
#include "sanitizer_local_address_space_view.h"
#include "sanitizer_mutex.h"
#include "sanitizer_procmaps.h"
#include "sanitizer_type_traits.h"

namespace __sanitizer {

// Allows the tools to name their allocations appropriately.
extern const char *PrimaryAllocatorName;
extern const char *SecondaryAllocatorName;

// Since flags are immutable and allocator behavior can be changed at runtime
// (unit tests or ASan on Android are some examples), allocator_may_return_null
// flag value is cached here and can be altered later.
bool AllocatorMayReturnNull();
void SetAllocatorMayReturnNull(bool may_return_null);

// Returns true if allocator detected OOM condition. Can be used to avoid memory
// hungry operations.
bool IsAllocatorOutOfMemory();
// Should be called by a particular allocator when OOM is detected.
void SetAllocatorOutOfMemory();

void PrintHintAllocatorCannotReturnNull();

// Callback type for iterating over chunks.
typedef void (*ForEachChunkCallback)(uptr chunk, void *arg);

inline u32 Rand(u32 *state) {  // ANSI C linear congruential PRNG.
  return (*state = *state * 1103515245 + 12345) >> 16;
}

inline u32 RandN(u32 *state, u32 n) { return Rand(state) % n; }  // [0, n)

template<typename T>
inline void RandomShuffle(T *a, u32 n, u32 *rand_state) {
  if (n <= 1) return;
  u32 state = *rand_state;
  for (u32 i = n - 1; i > 0; i--)
    Swap(a[i], a[RandN(&state, i + 1)]);
  *rand_state = state;
}

struct NoOpMapUnmapCallback {
  void OnMap(uptr p, uptr size) const {}
  void OnMapSecondary(uptr p, uptr size, uptr user_begin,
                      uptr user_size) const {}
  void OnUnmap(uptr p, uptr size) const {}
};

#include "sanitizer_allocator_size_class_map.h"
#include "sanitizer_allocator_stats.h"
#include "sanitizer_allocator_primary64.h"
#include "sanitizer_allocator_primary32.h"
#include "sanitizer_allocator_local_cache.h"
#include "sanitizer_allocator_secondary.h"
#include "sanitizer_allocator_combined.h"

bool IsRssLimitExceeded();
void SetRssLimitExceeded(bool limit_exceeded);

} // namespace __sanitizer

#endif // SANITIZER_ALLOCATOR_H
