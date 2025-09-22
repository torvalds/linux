//===- HexagonLoopIdiomRecognition.h --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONLOOPIDIOMRECOGNITION_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONLOOPIDIOMRECOGNITION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

struct HexagonLoopIdiomRecognitionPass
    : PassInfoMixin<HexagonLoopIdiomRecognitionPass> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONLOOPIDIOMRECOGNITION_H
