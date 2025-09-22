//===- llvm/CodeGen/DeadMachineInstructionElim.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DEADMACHINEINSTRUCTIONELIM_H
#define LLVM_CODEGEN_DEADMACHINEINSTRUCTIONELIM_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

class DeadMachineInstructionElimPass
    : public PassInfoMixin<DeadMachineInstructionElimPass> {
public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_DEADMACHINEINSTRUCTIONELIM_H
