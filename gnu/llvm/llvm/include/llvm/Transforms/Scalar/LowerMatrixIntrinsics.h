//===- LowerMatrixIntrinsics.h - Lower matrix intrinsics. -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers matrix intrinsics down to vector operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERMATRIXINTRINSICS_H
#define LLVM_TRANSFORMS_SCALAR_LOWERMATRIXINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class LowerMatrixIntrinsicsPass
    : public PassInfoMixin<LowerMatrixIntrinsicsPass> {
  bool Minimal;

public:
  LowerMatrixIntrinsicsPass(bool Minimal = false) : Minimal(Minimal) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
  static bool isRequired() { return true; }
};
} // namespace llvm

#endif
