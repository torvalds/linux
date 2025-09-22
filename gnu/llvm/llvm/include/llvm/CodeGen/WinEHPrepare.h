//===-- llvm/CodeGen/WinEHPrepare.h ----------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WINEHPREPARE_H
#define LLVM_CODEGEN_WINEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class WinEHPreparePass : public PassInfoMixin<WinEHPreparePass> {
  bool DemoteCatchSwitchPHIOnly;

public:
  WinEHPreparePass(bool DemoteCatchSwitchPHIOnly_ = false)
      : DemoteCatchSwitchPHIOnly(DemoteCatchSwitchPHIOnly_) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_WINEHPREPARE_H
