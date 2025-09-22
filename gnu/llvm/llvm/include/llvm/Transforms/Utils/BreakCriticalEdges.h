//===- BreakCriticalEdges.h - Critical Edge Elimination Pass --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BreakCriticalEdges pass - Break all of the critical edges in the CFG by
// inserting a dummy basic block.  This pass may be "required" by passes that
// cannot deal with critical edges.  For this usage, the structure type is
// forward declared.  This pass obviously invalidates the CFG, but can update
// dominator trees.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BREAKCRITICALEDGES_H
#define LLVM_TRANSFORMS_UTILS_BREAKCRITICALEDGES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
struct BreakCriticalEdgesPass : public PassInfoMixin<BreakCriticalEdgesPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm
#endif // LLVM_TRANSFORMS_UTILS_BREAKCRITICALEDGES_H
