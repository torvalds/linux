//===- LoopSimplifyCFG.cpp - Loop CFG Simplification Pass -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Loop SimplifyCFG Pass. This pass is responsible for
// basic loop CFG cleanup, primarily to assist other loop passes. If you
// encounter a noncanonical CFG construct that causes another loop pass to
// perform suboptimally, this is the place to fix it up.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPSIMPLIFYCFG_H
#define LLVM_TRANSFORMS_SCALAR_LOOPSIMPLIFYCFG_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class LPMUpdater;
class Loop;

/// Performs basic CFG simplifications to assist other loop passes.
class LoopSimplifyCFGPass : public PassInfoMixin<LoopSimplifyCFGPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPSIMPLIFYCFG_H
