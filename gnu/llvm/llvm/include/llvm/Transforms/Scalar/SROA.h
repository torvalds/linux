//===- SROA.h - Scalar Replacement Of Aggregates ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Scalar Replacement of
/// Aggregates pass. This pass provides both aggregate splitting and the
/// primary SSA formation used in the compiler.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SROA_H
#define LLVM_TRANSFORMS_SCALAR_SROA_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

enum class SROAOptions : bool { ModifyCFG, PreserveCFG };

class SROAPass : public PassInfoMixin<SROAPass> {
  const SROAOptions PreserveCFG;

public:
  /// If \p PreserveCFG is set, then the pass is not allowed to modify CFG
  /// in any way, even if it would update CFG analyses.
  SROAPass(SROAOptions PreserveCFG);

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SROA_H
