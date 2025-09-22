//===- ControlHeightReduction.h - Control Height Reduction ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass merges conditional blocks of code and reduces the number of
// conditional branches in the hot paths based on profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class ControlHeightReductionPass :
      public PassInfoMixin<ControlHeightReductionPass> {
public:
  ControlHeightReductionPass();
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
