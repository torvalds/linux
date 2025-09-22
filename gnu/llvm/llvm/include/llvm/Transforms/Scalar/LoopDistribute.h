//===- LoopDistribute.cpp - Loop Distribution Pass --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Loop Distribution Pass.  Its main focus is to
// distribute loops that cannot be vectorized due to dependence cycles.  It
// tries to isolate the offending dependences into a new loop allowing
// vectorization of the remaining parts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPDISTRIBUTE_H
#define LLVM_TRANSFORMS_SCALAR_LOOPDISTRIBUTE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class LoopDistributePass : public PassInfoMixin<LoopDistributePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPDISTRIBUTE_H
