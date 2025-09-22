//===- LowerSwitch.h - Eliminate Switch instructions ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LowerSwitch transformation rewrites switch instructions with a sequence
// of branches, which allows targets to get away with not implementing the
// switch instruction until it is convenient.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H
#define LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H

#include "llvm/IR/PassManager.h"

namespace llvm {
struct LowerSwitchPass : public PassInfoMixin<LowerSwitchPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H
