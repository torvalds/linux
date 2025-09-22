//===--- ExpandMemCmp.h - Expand memcmp() to load/stores --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXPANDMEMCMP_H
#define LLVM_CODEGEN_EXPANDMEMCMP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

class ExpandMemCmpPass : public PassInfoMixin<ExpandMemCmpPass> {
  const TargetMachine *TM;

public:
  explicit ExpandMemCmpPass(const TargetMachine *TM_) : TM(TM_) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_EXPANDMEMCMP_H
