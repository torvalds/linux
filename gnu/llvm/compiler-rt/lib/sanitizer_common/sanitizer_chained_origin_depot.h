//===-- sanitizer_chained_origin_depot.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A storage for chained origins.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_CHAINED_ORIGIN_DEPOT_H
#define SANITIZER_CHAINED_ORIGIN_DEPOT_H

#include "sanitizer_common.h"

namespace __sanitizer {

class ChainedOriginDepot {
 public:
  ChainedOriginDepot();

  // Gets the statistic of the origin chain storage.
  StackDepotStats GetStats() const;

  // Stores a chain with StackDepot ID here_id and previous chain ID prev_id.
  // If successful, returns true and the new chain id new_id.
  // If the same element already exists, returns false and sets new_id to the
  // existing ID.
  bool Put(u32 here_id, u32 prev_id, u32 *new_id);

  // Retrieves the stored StackDepot ID for the given origin ID.
  u32 Get(u32 id, u32 *other);

  void LockBeforeFork();
  void UnlockAfterFork(bool fork_child);
  void TestOnlyUnmap();

 private:
  ChainedOriginDepot(const ChainedOriginDepot &) = delete;
  void operator=(const ChainedOriginDepot &) = delete;
};

}  // namespace __sanitizer

#endif  // SANITIZER_CHAINED_ORIGIN_DEPOT_H
