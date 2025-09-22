//===- SimplifyCFG.h - Simplify and canonicalize the CFG --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the pass responsible for both
/// simplifying and canonicalizing the CFG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H
#define LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"

namespace llvm {

/// A pass to simplify and canonicalize the CFG of a function.
///
/// This pass iteratively simplifies the entire CFG of a function. It may change
/// or remove control flow to put the CFG into a canonical form expected by
/// other passes of the mid-level optimizer. Depending on the specified options,
/// it may further optimize control-flow to create non-canonical forms.
class SimplifyCFGPass : public PassInfoMixin<SimplifyCFGPass> {
  SimplifyCFGOptions Options;

public:
  /// The default constructor sets the pass options to create canonical IR,
  /// rather than optimal IR. That is, by default we bypass transformations that
  /// are likely to improve performance but make analysis for other passes more
  /// difficult.
  SimplifyCFGPass();

  /// Construct a pass with optional optimizations.
  SimplifyCFGPass(const SimplifyCFGOptions &PassOptions);

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

}

#endif
