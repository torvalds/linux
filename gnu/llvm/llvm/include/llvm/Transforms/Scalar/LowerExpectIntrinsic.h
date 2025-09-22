//===- LowerExpectIntrinsic.h - LowerExpectIntrinsic pass -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// The header file for the LowerExpectIntrinsic pass as used by the new pass
/// manager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWEREXPECTINTRINSIC_H
#define LLVM_TRANSFORMS_SCALAR_LOWEREXPECTINTRINSIC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct LowerExpectIntrinsicPass : PassInfoMixin<LowerExpectIntrinsicPass> {
  /// Run the pass over the function.
  ///
  /// This will lower all of the expect intrinsic calls in this function into
  /// branch weight metadata. That metadata will subsequently feed the analysis
  /// of the probabilities and frequencies of the CFG. After running this pass,
  /// no more expect intrinsics remain, allowing the rest of the optimizer to
  /// ignore them.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};

}

#endif
