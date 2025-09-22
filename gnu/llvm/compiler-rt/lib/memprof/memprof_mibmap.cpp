//===-- memprof_mibmap.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
//===----------------------------------------------------------------------===//

#include "memprof_mibmap.h"
#include "profile/MemProfData.inc"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_mutex.h"

namespace __memprof {
using ::llvm::memprof::MemInfoBlock;

void InsertOrMerge(const uptr Id, const MemInfoBlock &Block, MIBMapTy &Map) {
  MIBMapTy::Handle h(&Map, static_cast<uptr>(Id), /*remove=*/false,
                     /*create=*/true);
  if (h.created()) {
    LockedMemInfoBlock *lmib =
        (LockedMemInfoBlock *)InternalAlloc(sizeof(LockedMemInfoBlock));
    lmib->mutex.Init();
    lmib->mib = Block;
    *h = lmib;
  } else {
    LockedMemInfoBlock *lmib = *h;
    SpinMutexLock lock(&lmib->mutex);
    uintptr_t ShorterHistogram;
    if (Block.AccessHistogramSize > lmib->mib.AccessHistogramSize)
      ShorterHistogram = lmib->mib.AccessHistogram;
    else
      ShorterHistogram = Block.AccessHistogram;

    lmib->mib.Merge(Block);
    // The larger histogram is kept and the shorter histogram is discarded after
    // adding the counters to the larger historam. Free only the shorter
    // Histogram
    if (Block.AccessHistogramSize > 0 || lmib->mib.AccessHistogramSize > 0)
      InternalFree((void *)ShorterHistogram);
  }
}

} // namespace __memprof
