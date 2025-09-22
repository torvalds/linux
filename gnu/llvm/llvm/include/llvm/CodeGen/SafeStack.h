//===--------------------- llvm/CodeGen/SafeStack.h -------------*- C++-*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SAFESTACK_H
#define LLVM_CODEGEN_SAFESTACK_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

class SafeStackPass : public PassInfoMixin<SafeStackPass> {
  const TargetMachine *TM;

public:
  explicit SafeStackPass(const TargetMachine *TM_) : TM(TM_) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SAFESTACK_H
