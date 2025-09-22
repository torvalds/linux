//===------- LeonPasses.h - Define passes specific to LEON ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_LEON_PASSES_H
#define LLVM_LIB_TARGET_SPARC_LEON_PASSES_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
class SparcSubtarget;

class LLVM_LIBRARY_VISIBILITY LEONMachineFunctionPass
    : public MachineFunctionPass {
protected:
  const SparcSubtarget *Subtarget = nullptr;
  const int LAST_OPERAND = -1;

  // this vector holds free registers that we allocate in groups for some of the
  // LEON passes
  std::vector<int> UsedRegisters;

protected:
  LEONMachineFunctionPass(char &ID);

  void clearUsedRegisterList() { UsedRegisters.clear(); }

  void markRegisterUsed(int registerIndex) {
    UsedRegisters.push_back(registerIndex);
  }
};

class LLVM_LIBRARY_VISIBILITY InsertNOPLoad : public LEONMachineFunctionPass {
public:
  static char ID;

  InsertNOPLoad();
  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "InsertNOPLoad: Erratum Fix LBR35: insert a NOP instruction after "
           "every single-cycle load instruction when the next instruction is "
           "another load/store instruction";
  }
};

class LLVM_LIBRARY_VISIBILITY DetectRoundChange
    : public LEONMachineFunctionPass {
public:
  static char ID;

  DetectRoundChange();
  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "DetectRoundChange: Leon erratum detection: detect any rounding "
           "mode change request: use only the round-to-nearest rounding mode";
  }
};

class LLVM_LIBRARY_VISIBILITY FixAllFDIVSQRT : public LEONMachineFunctionPass {
public:
  static char ID;

  FixAllFDIVSQRT();
  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "FixAllFDIVSQRT: Erratum Fix LBR34: fix FDIVS/FDIVD/FSQRTS/FSQRTD "
           "instructions with NOPs and floating-point store";
  }
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_SPARC_LEON_PASSES_H
