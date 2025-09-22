//===---- llvm/CodeGen/InterleavedAccess.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the InterleavedAccessPass class,
/// its corresponding pass name is `interleaved-access`.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INTERLEAVEDACCESS_H
#define LLVM_CODEGEN_INTERLEAVEDACCESS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

class InterleavedAccessPass : public PassInfoMixin<InterleavedAccessPass> {
  const TargetMachine *TM;

public:
  explicit InterleavedAccessPass(const TargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_INTERLEAVEDACCESS_H
