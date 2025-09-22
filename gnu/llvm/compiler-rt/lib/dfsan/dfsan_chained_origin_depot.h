//===-- dfsan_chained_origin_depot.h ----------------------------*- C++ -*-===//
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

#ifndef DFSAN_CHAINED_ORIGIN_DEPOT_H
#define DFSAN_CHAINED_ORIGIN_DEPOT_H

#include "sanitizer_common/sanitizer_chained_origin_depot.h"
#include "sanitizer_common/sanitizer_common.h"

namespace __dfsan {

ChainedOriginDepot* GetChainedOriginDepot();

void ChainedOriginDepotLockBeforeFork();
void ChainedOriginDepotUnlockAfterFork(bool fork_child);

}  // namespace __dfsan

#endif  // DFSAN_CHAINED_ORIGIN_DEPOT_H
