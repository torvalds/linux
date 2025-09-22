//===- FlattenCFG.h -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The FlattenCFG pass flattens a function's CFG using the FlattenCFG utility
// function, iteratively flattening until no further changes are made.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_FLATTENCFG_H
#define LLVM_TRANSFORMS_SCALAR_FLATTENCFG_H

#include "llvm/IR/PassManager.h"

namespace llvm {
struct FlattenCFGPass : PassInfoMixin<FlattenCFGPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_FLATTENCFG_H
