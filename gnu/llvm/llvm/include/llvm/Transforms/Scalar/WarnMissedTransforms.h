//===- WarnMissedTransforms.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit warnings if forced code transformations have not been performed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
#define LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
// New pass manager boilerplate.
class WarnMissedTransformationsPass
    : public PassInfoMixin<WarnMissedTransformationsPass> {
public:
  explicit WarnMissedTransformationsPass() = default;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
