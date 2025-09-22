//===- BoundsChecking.h - Bounds checking instrumentation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;

/// A pass to instrument code and perform run-time bounds checking on loads,
/// stores, and other memory intrinsics.
struct BoundsCheckingPass : PassInfoMixin<BoundsCheckingPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
