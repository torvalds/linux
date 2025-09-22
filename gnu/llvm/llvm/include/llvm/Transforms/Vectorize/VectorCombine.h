//===-------- VectorCombine.h - Optimize partial vector operations --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass optimizes scalar/vector interactions using target cost models. The
// transforms implemented here may not fit in traditional loop-based or SLP
// vectorization passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VECTORCOMBINE_H
#define LLVM_TRANSFORMS_VECTORIZE_VECTORCOMBINE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Optimize scalar/vector interactions in IR using target cost models.
class VectorCombinePass : public PassInfoMixin<VectorCombinePass> {
  /// If true, only perform beneficial early IR transforms. Do not introduce new
  /// vector operations.
  bool TryEarlyFoldsOnly;

public:
  VectorCombinePass(bool TryEarlyFoldsOnly = false)
      : TryEarlyFoldsOnly(TryEarlyFoldsOnly) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};
}
#endif // LLVM_TRANSFORMS_VECTORIZE_VECTORCOMBINE_H
