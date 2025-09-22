//===-- dfsan_chained_origin_depot.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// A storage for chained origins.
//===----------------------------------------------------------------------===//

#include "dfsan_chained_origin_depot.h"

namespace __dfsan {

static ChainedOriginDepot chainedOriginDepot;

ChainedOriginDepot* GetChainedOriginDepot() { return &chainedOriginDepot; }

void ChainedOriginDepotLockBeforeFork() { chainedOriginDepot.LockBeforeFork(); }

void ChainedOriginDepotUnlockAfterFork(bool fork_child) {
  chainedOriginDepot.UnlockAfterFork(fork_child);
}

}  // namespace __dfsan
