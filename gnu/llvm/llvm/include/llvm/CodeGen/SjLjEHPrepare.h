//===-- llvm/CodeGen/SjLjEHPrepare.h -------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SJLJEHPREPARE_H
#define LLVM_CODEGEN_SJLJEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

class SjLjEHPreparePass : public PassInfoMixin<SjLjEHPreparePass> {
  const TargetMachine *TM;

public:
  explicit SjLjEHPreparePass(const TargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SJLJEHPREPARE_H
