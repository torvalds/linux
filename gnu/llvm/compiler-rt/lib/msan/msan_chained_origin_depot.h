//===-- msan_chained_origin_depot.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// A storage for chained origins.
//===----------------------------------------------------------------------===//

#ifndef MSAN_CHAINED_ORIGIN_DEPOT_H
#define MSAN_CHAINED_ORIGIN_DEPOT_H

#include "sanitizer_common/sanitizer_common.h"

namespace __msan {

// Gets the statistic of the origin chain storage.
StackDepotStats ChainedOriginDepotGetStats();

// Stores a chain with StackDepot ID here_id and previous chain ID prev_id.
// If successful, returns true and the new chain id new_id.
// If the same element already exists, returns false and sets new_id to the
// existing ID.
bool ChainedOriginDepotPut(u32 here_id, u32 prev_id, u32 *new_id);

// Retrieves the stored StackDepot ID for the given origin ID.
u32 ChainedOriginDepotGet(u32 id, u32 *other);

void ChainedOriginDepotBeforeFork();
void ChainedOriginDepotAfterFork(bool fork_child);

}  // namespace __msan

#endif  // MSAN_CHAINED_ORIGIN_DEPOT_H
