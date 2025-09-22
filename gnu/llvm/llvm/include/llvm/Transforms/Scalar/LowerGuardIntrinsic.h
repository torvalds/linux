//===--- LowerGuardIntrinsic.h - Lower the guard intrinsic ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the llvm.experimental.guard intrinsic to a conditional call
// to @llvm.experimental.deoptimize.  Once this happens, the guard can no longer
// be widened.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H
#define LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct LowerGuardIntrinsicPass : PassInfoMixin<LowerGuardIntrinsicPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H
