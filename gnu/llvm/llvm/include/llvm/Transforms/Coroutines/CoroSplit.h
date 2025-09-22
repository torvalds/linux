//===- CoroSplit.h - Converts a coroutine into a state machine -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file declares the pass that builds the coroutine frame and outlines
// the resume and destroy parts of the coroutine into separate functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H
#define LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

struct CoroSplitPass : PassInfoMixin<CoroSplitPass> {
  const std::function<bool(Instruction &)> MaterializableCallback;

  CoroSplitPass(bool OptimizeFrame = false);
  CoroSplitPass(std::function<bool(Instruction &)> MaterializableCallback,
                bool OptimizeFrame = false)
      : MaterializableCallback(MaterializableCallback),
        OptimizeFrame(OptimizeFrame) {}

  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);
  static bool isRequired() { return true; }

  // Would be true if the Optimization level isn't O0.
  bool OptimizeFrame;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H
