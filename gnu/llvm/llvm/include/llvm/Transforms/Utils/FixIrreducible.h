//===- FixIrreducible.h - Convert irreducible control-flow into loops -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H
#define LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
struct FixIrreduciblePass : PassInfoMixin<FixIrreduciblePass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H
