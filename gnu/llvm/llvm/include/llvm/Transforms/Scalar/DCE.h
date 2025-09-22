//===- DCE.h - Dead code elimination ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Dead Code Elimination pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DCE_H
#define LLVM_TRANSFORMS_SCALAR_DCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Basic Dead Code Elimination pass.
class DCEPass : public PassInfoMixin<DCEPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

class RedundantDbgInstEliminationPass
    : public PassInfoMixin<RedundantDbgInstEliminationPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_DCE_H
