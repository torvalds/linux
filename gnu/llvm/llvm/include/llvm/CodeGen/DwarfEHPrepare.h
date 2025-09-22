//===------------------- llvm/CodeGen/DwarfEHPrepare.h ----------*- C++-*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass mulches exception handling code into a form adapted to code
// generation. Required if using dwarf exception handling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFEHPREPARE_H
#define LLVM_CODEGEN_DWARFEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

class DwarfEHPreparePass : public PassInfoMixin<DwarfEHPreparePass> {
  const TargetMachine *TM;

public:
  explicit DwarfEHPreparePass(const TargetMachine *TM_) : TM(TM_) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_DWARFEHPREPARE_H
