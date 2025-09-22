//===- LoopSink.h - Loop Sink Pass ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Sink pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPSINK_H
#define LLVM_TRANSFORMS_SCALAR_LOOPSINK_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// A pass that does profile-guided sinking of instructions into loops.
///
/// This is a function pass as it shouldn't be composed into any kind of
/// unified loop pass pipeline. The goal of it is to sink code into loops that
/// is loop invariant but only required within the loop body when doing so
/// reduces the global expected dynamic frequency with which it executes.
/// A classic example is an extremely cold branch within a loop body.
///
/// We do this as a separate pass so that during normal optimization all
/// invariant operations can be held outside the loop body to simplify
/// fundamental analyses and transforms of the loop.
class LoopSinkPass : public PassInfoMixin<LoopSinkPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPSINK_H
