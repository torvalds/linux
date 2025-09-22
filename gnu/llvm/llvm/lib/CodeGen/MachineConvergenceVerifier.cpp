//===- MachineConvergenceVerifier.cpp - Verify convergencectrl ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineConvergenceVerifier.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/IR/GenericConvergenceVerifierImpl.h"

using namespace llvm;

template <>
auto GenericConvergenceVerifier<MachineSSAContext>::getConvOp(
    const MachineInstr &MI) -> ConvOpKind {
  switch (MI.getOpcode()) {
  default:
    return CONV_NONE;
  case TargetOpcode::CONVERGENCECTRL_ENTRY:
    return CONV_ENTRY;
  case TargetOpcode::CONVERGENCECTRL_ANCHOR:
    return CONV_ANCHOR;
  case TargetOpcode::CONVERGENCECTRL_LOOP:
    return CONV_LOOP;
  }
}

template <>
void GenericConvergenceVerifier<
    MachineSSAContext>::checkConvergenceTokenProduced(const MachineInstr &MI) {
  Check(!MI.hasImplicitDef(),
        "Convergence control tokens are defined explicitly.",
        {Context.print(&MI)});
  const MachineOperand &Def = MI.getOperand(0);
  const MachineRegisterInfo &MRI = Context.getFunction()->getRegInfo();
  Check(MRI.getUniqueVRegDef(Def.getReg()),
        "Convergence control tokens must have unique definitions.",
        {Context.print(&MI)});
}

template <>
const MachineInstr *
GenericConvergenceVerifier<MachineSSAContext>::findAndCheckConvergenceTokenUsed(
    const MachineInstr &MI) {
  const MachineRegisterInfo &MRI = Context.getFunction()->getRegInfo();
  const MachineInstr *TokenDef = nullptr;

  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isUse())
      continue;
    Register OpReg = MO.getReg();
    if (!OpReg.isVirtual())
      continue;

    const MachineInstr *Def = MRI.getUniqueVRegDef(OpReg);
    if (!Def)
      continue;
    if (getConvOp(*Def) == CONV_NONE)
      continue;

    CheckOrNull(
        MI.isConvergent(),
        "Convergence control tokens can only be used by convergent operations.",
        {Context.print(OpReg), Context.print(&MI)});

    CheckOrNull(!TokenDef,
                "An operation can use at most one convergence control token.",
                {Context.print(OpReg), Context.print(&MI)});

    TokenDef = Def;
  }

  if (TokenDef)
    Tokens[&MI] = TokenDef;

  return TokenDef;
}

template <>
bool GenericConvergenceVerifier<MachineSSAContext>::isInsideConvergentFunction(
    const MachineInstr &MI) {
  // The class MachineFunction does not have any property to indicate whether it
  // is convergent. Trivially return true so that the check always passes.
  return true;
}

template <>
bool GenericConvergenceVerifier<MachineSSAContext>::isConvergent(
    const MachineInstr &MI) {
  return MI.isConvergent();
}

template class llvm::GenericConvergenceVerifier<MachineSSAContext>;
