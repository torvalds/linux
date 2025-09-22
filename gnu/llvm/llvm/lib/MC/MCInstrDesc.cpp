//===------ llvm/MC/MCInstrDesc.cpp- Instruction Descriptors --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines methods on the MCOperandInfo and MCInstrDesc classes, which
// are used to describe target instructions and their operands.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"

using namespace llvm;

bool MCInstrDesc::mayAffectControlFlow(const MCInst &MI,
                                       const MCRegisterInfo &RI) const {
  if (isBranch() || isCall() || isReturn() || isIndirectBranch())
    return true;
  unsigned PC = RI.getProgramCounter();
  if (PC == 0)
    return false;
  if (hasDefOfPhysReg(MI, PC, RI))
    return true;
  return false;
}

bool MCInstrDesc::hasImplicitDefOfPhysReg(unsigned Reg,
                                          const MCRegisterInfo *MRI) const {
  for (MCPhysReg ImpDef : implicit_defs())
    if (ImpDef == Reg || (MRI && MRI->isSubRegister(Reg, ImpDef)))
      return true;
  return false;
}

bool MCInstrDesc::hasDefOfPhysReg(const MCInst &MI, unsigned Reg,
                                  const MCRegisterInfo &RI) const {
  for (int i = 0, e = NumDefs; i != e; ++i)
    if (MI.getOperand(i).isReg() && MI.getOperand(i).getReg() &&
        RI.isSubRegisterEq(Reg, MI.getOperand(i).getReg()))
      return true;
  if (variadicOpsAreDefs())
    for (int i = NumOperands - 1, e = MI.getNumOperands(); i != e; ++i)
      if (MI.getOperand(i).isReg() &&
          RI.isSubRegisterEq(Reg, MI.getOperand(i).getReg()))
        return true;
  return hasImplicitDefOfPhysReg(Reg, &RI);
}
