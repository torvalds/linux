//===-- dfsan_origin.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// Origin id utils.
//===----------------------------------------------------------------------===//

#ifndef DFSAN_ORIGIN_H
#define DFSAN_ORIGIN_H

#include "dfsan_chained_origin_depot.h"
#include "dfsan_flags.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __dfsan {

// Origin handling.
//
// Origin is a 32-bit identifier that is attached to any taint value in the
// program and describes how this memory came to be tainted.
//
// Chained origin id is like:
// zzzz xxxx xxxx xxxx
//
// Chained origin id describes an event of storing a taint value to
// memory. The xxx part is a value of ChainedOriginDepot, which is a mapping of
// (stack_id, prev_id) -> id, where
//  * stack_id describes the event.
//    StackDepot keeps a mapping between those and corresponding stack traces.
//  * prev_id is another origin id that describes the earlier part of the
//    taint value history. 0 prev_id indicates the start of a chain.
// Following a chain of prev_id provides the full recorded history of a taint
// value.
//
// This, effectively, defines a forest where nodes are points in value history
// marked with origin ids, and edges are events that are marked with stack_id.
//
// The "zzzz" bits of chained origin id are used to store the length of the
// origin chain.

class Origin {
 public:
  static bool isValidId(u32 id) { return id != 0; }

  u32 raw_id() const { return raw_id_; }

  bool isChainedOrigin() const { return Origin::isValidId(raw_id_); }

  u32 getChainedId() const {
    CHECK(Origin::isValidId(raw_id_));
    return raw_id_ & kChainedIdMask;
  }

  // Returns the next origin in the chain and the current stack trace.
  //
  // It scans a partition of StackDepot linearly, and is used only by origin
  // tracking report.
  Origin getNextChainedOrigin(StackTrace *stack) const {
    CHECK(Origin::isValidId(raw_id_));
    u32 prev_id;
    u32 stack_id = GetChainedOriginDepot()->Get(getChainedId(), &prev_id);
    if (stack)
      *stack = StackDepotGet(stack_id);
    return Origin(prev_id);
  }

  static Origin CreateChainedOrigin(Origin prev, StackTrace *stack) {
    int depth = prev.isChainedOrigin() ? prev.depth() : -1;
    // depth is the length of the chain minus 1.
    // origin_history_size of 0 means unlimited depth.
    if (flags().origin_history_size > 0) {
      ++depth;
      if (depth >= flags().origin_history_size || depth > kMaxDepth)
        return prev;
    }

    StackDepotHandle h = StackDepotPut_WithHandle(*stack);
    if (!h.valid())
      return prev;

    if (flags().origin_history_per_stack_limit > 0) {
      int use_count = h.use_count();
      if (use_count > flags().origin_history_per_stack_limit)
        return prev;
    }

    u32 chained_id;
    bool inserted =
        GetChainedOriginDepot()->Put(h.id(), prev.raw_id(), &chained_id);
    CHECK((chained_id & kChainedIdMask) == chained_id);

    if (inserted && flags().origin_history_per_stack_limit > 0)
      h.inc_use_count_unsafe();

    return Origin((depth << kDepthShift) | chained_id);
  }

  static Origin FromRawId(u32 id) { return Origin(id); }

 private:
  static const int kDepthBits = 4;
  static const int kDepthShift = 32 - kDepthBits;

  static const u32 kChainedIdMask = ((u32)-1) >> kDepthBits;

  u32 raw_id_;

  explicit Origin(u32 raw_id) : raw_id_(raw_id) {}

  int depth() const {
    CHECK(isChainedOrigin());
    return (raw_id_ >> kDepthShift) & ((1 << kDepthBits) - 1);
  }

 public:
  static const int kMaxDepth = (1 << kDepthBits) - 1;
};

}  // namespace __dfsan

#endif  // DFSAN_ORIGIN_H
