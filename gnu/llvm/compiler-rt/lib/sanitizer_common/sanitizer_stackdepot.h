//===-- sanitizer_stackdepot.h ----------------------------------*- C++ -*-===//
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

#ifndef SANITIZER_STACKDEPOT_H
#define SANITIZER_STACKDEPOT_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

// StackDepot efficiently stores huge amounts of stack traces.
struct StackDepotNode;
struct StackDepotHandle {
  StackDepotNode *node_ = nullptr;
  u32 id_ = 0;
  StackDepotHandle(StackDepotNode *node, u32 id) : node_(node), id_(id) {}
  bool valid() const { return node_; }
  u32 id() const { return id_; }
  int use_count() const;
  void inc_use_count_unsafe();
};

const int kStackDepotMaxUseCount = 1U << (SANITIZER_ANDROID ? 16 : 20);

StackDepotStats StackDepotGetStats();
u32 StackDepotPut(StackTrace stack);
StackDepotHandle StackDepotPut_WithHandle(StackTrace stack);
// Retrieves a stored stack trace by the id.
StackTrace StackDepotGet(u32 id);

void StackDepotLockBeforeFork();
void StackDepotUnlockAfterFork(bool fork_child);
void StackDepotPrintAll();
void StackDepotStopBackgroundThread();

void StackDepotTestOnlyUnmap();

} // namespace __sanitizer

#endif // SANITIZER_STACKDEPOT_H
