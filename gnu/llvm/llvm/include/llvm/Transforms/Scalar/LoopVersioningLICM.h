//===- LoopVersioningLICM.h - LICM Loop Versioning ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;

class LoopVersioningLICMPass : public PassInfoMixin<LoopVersioningLICMPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &LAR, LPMUpdater &U);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H
