//===-- CFGuard.h - CFGuard Transformations ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
// Windows Control Flow Guard passes (/guard:cf).
//===---------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_CFGUARD_H
#define LLVM_TRANSFORMS_CFGUARD_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class FunctionPass;

class CFGuardPass : public PassInfoMixin<CFGuardPass> {
public:
  enum class Mechanism { Check, Dispatch };

  CFGuardPass(Mechanism M = Mechanism::Check) : GuardMechanism(M) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

private:
  Mechanism GuardMechanism;
};

/// Insert Control FLow Guard checks on indirect function calls.
FunctionPass *createCFGuardCheckPass();

/// Insert Control FLow Guard dispatches on indirect function calls.
FunctionPass *createCFGuardDispatchPass();

} // namespace llvm

#endif
