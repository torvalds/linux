//===- CodeGenPrepare.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for CodeGen Prepare.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PREPARE_H
#define LLVM_CODEGEN_PREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

class CodeGenPreparePass : public PassInfoMixin<CodeGenPreparePass> {
private:
  const TargetMachine *TM;

public:
  CodeGenPreparePass(const TargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PREPARE_H
