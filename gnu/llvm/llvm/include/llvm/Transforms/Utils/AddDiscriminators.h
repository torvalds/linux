//===- AddDiscriminators.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass adds DWARF discriminators to the IR. Path discriminators are used
// to decide what CFG path was taken inside sub-graphs whose instructions share
// the same line and column number information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
#define LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class AddDiscriminatorsPass : public PassInfoMixin<AddDiscriminatorsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
