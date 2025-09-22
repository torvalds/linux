//===- InferAlignment.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Infer alignment for load, stores and other memory operations based on
// trailing zero known bits information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H
#define LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct InferAlignmentPass : public PassInfoMixin<InferAlignmentPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H
