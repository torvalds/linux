//===--- LowerWidenableCondition.h - Lower the guard intrinsic ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the llvm.widenable.condition intrinsic to default value
// which is i1 true.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_SCALAR_LOWERWIDENABLECONDITION_H
#define LLVM_TRANSFORMS_SCALAR_LOWERWIDENABLECONDITION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct LowerWidenableConditionPass : PassInfoMixin<LowerWidenableConditionPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_SCALAR_LOWERWIDENABLECONDITION_H
