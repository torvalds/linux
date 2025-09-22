//===- LoopIdiomRecognize.h - Loop Idiom Recognize Pass ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements an idiom recognizer that transforms simple loops into a
// non-loop form.  In cases that this kicks in, it can be a significant
// performance win.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPIDIOMRECOGNIZE_H
#define LLVM_TRANSFORMS_SCALAR_LOOPIDIOMRECOGNIZE_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LPMUpdater;

/// Options to disable Loop Idiom Recognize, which can be shared with other
/// passes.
struct DisableLIRP {
  /// When true, the entire pass is disabled.
  static bool All;

  /// When true, Memset is disabled.
  static bool Memset;

  /// When true, Memcpy is disabled.
  static bool Memcpy;
};

/// Performs Loop Idiom Recognize Pass.
class LoopIdiomRecognizePass : public PassInfoMixin<LoopIdiomRecognizePass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPIDIOMRECOGNIZE_H
