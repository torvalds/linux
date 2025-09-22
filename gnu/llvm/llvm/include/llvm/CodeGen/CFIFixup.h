//===-- CFIFixup.h - Insert CFI remember/restore instructions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains definition of the base CFIFixup pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CFIFIXUP_H
#define LLVM_CODEGEN_CFIFIXUP_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"

namespace llvm {
class CFIFixup : public MachineFunctionPass {
public:
  static char ID;

  CFIFixup() : MachineFunctionPass(ID) {
    initializeCFIFixupPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace llvm

#endif // LLVM_CODEGEN_CFIFIXUP_H
