//===----------LoopIdiomVectorize.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H
#define LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {
enum class LoopIdiomVectorizeStyle { Masked, Predicated };

class LoopIdiomVectorizePass : public PassInfoMixin<LoopIdiomVectorizePass> {
  LoopIdiomVectorizeStyle VectorizeStyle = LoopIdiomVectorizeStyle::Masked;

  // The VF used in vectorizing the byte compare pattern.
  unsigned ByteCompareVF = 16;

public:
  LoopIdiomVectorizePass() = default;
  explicit LoopIdiomVectorizePass(LoopIdiomVectorizeStyle S)
      : VectorizeStyle(S) {}

  LoopIdiomVectorizePass(LoopIdiomVectorizeStyle S, unsigned BCVF)
      : VectorizeStyle(S), ByteCompareVF(BCVF) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};
} // namespace llvm
#endif // LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H
