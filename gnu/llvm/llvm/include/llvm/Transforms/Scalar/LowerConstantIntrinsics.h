//===- LowerConstantIntrinsics.h - Lower constant int. pass -*- C++ -*-========//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// The header file for the LowerConstantIntrinsics pass as used by the new pass
/// manager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERCONSTANTINTRINSICS_H
#define LLVM_TRANSFORMS_SCALAR_LOWERCONSTANTINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct LowerConstantIntrinsicsPass :
    PassInfoMixin<LowerConstantIntrinsicsPass> {
public:
  explicit LowerConstantIntrinsicsPass() = default;

  /// Run the pass over the function.
  ///
  /// This will lower all remaining 'objectsize' and 'is.constant'`
  /// intrinsic calls in this function, even when the argument has no known
  /// size or is not a constant respectively. The resulting constant is
  /// propagated and conditional branches are resolved where possible.
  /// This complements the Instruction Simplification and
  /// Instruction Combination passes of the optimized pass chain.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};

}

#endif
