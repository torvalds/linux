//===-- UnreachableBlockElim.h - Remove unreachable blocks for codegen --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is an extremely simple version of the SimplifyCFG pass.  Its sole
// job is to delete LLVM basic blocks that are not reachable from the entry
// node.  To do this, it performs a simple depth first traversal of the CFG,
// then deletes any unvisited nodes.
//
// Note that this pass is really a hack.  In particular, the instruction
// selectors for various targets should just not generate code for unreachable
// blocks.  Until LLVM has a more systematic way of defining instruction
// selectors, however, we cannot really expect them to handle additional
// complexity.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_UNREACHABLEBLOCKELIM_H
#define LLVM_CODEGEN_UNREACHABLEBLOCKELIM_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class UnreachableBlockElimPass
    : public PassInfoMixin<UnreachableBlockElimPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_CODEGEN_UNREACHABLEBLOCKELIM_H
