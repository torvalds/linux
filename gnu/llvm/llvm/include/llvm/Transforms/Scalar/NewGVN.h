//===- NewGVN.h - Global Value Numbering Pass -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for LLVM's Global Value Numbering pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_NEWGVN_H
#define LLVM_TRANSFORMS_SCALAR_NEWGVN_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class NewGVNPass : public PassInfoMixin<NewGVNPass> {
public:
  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, AnalysisManager<Function> &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_NEWGVN_H

