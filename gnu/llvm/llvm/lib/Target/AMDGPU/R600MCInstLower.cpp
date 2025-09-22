//===- R600MCInstLower.cpp - Lower R600 MachineInstr to an MCInst ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Code to lower R600 MachineInstrs to their corresponding MCInst.
//
//===----------------------------------------------------------------------===//
//

#include "AMDGPUMCInstLower.h"
#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600AsmPrinter.h"
#include "R600Subtarget.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"

namespace {
class R600MCInstLower : public AMDGPUMCInstLower {
public:
  R600MCInstLower(MCContext &ctx, const R600Subtarget &ST,
                  const AsmPrinter &AP);

  /// Lower a MachineInstr to an MCInst
  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // namespace

R600MCInstLower::R600MCInstLower(MCContext &Ctx, const R600Subtarget &ST,
                                 const AsmPrinter &AP)
    : AMDGPUMCInstLower(Ctx, ST, AP) {}

void R600MCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->explicit_operands()) {
    MCOperand MCOp;
    lowerOperand(MO, MCOp);
    OutMI.addOperand(MCOp);
  }
}

void R600AsmPrinter::emitInstruction(const MachineInstr *MI) {
  R600_MC::verifyInstructionPredicates(MI->getOpcode(),
                                       getSubtargetInfo().getFeatureBits());

  const R600Subtarget &STI = MF->getSubtarget<R600Subtarget>();
  R600MCInstLower MCInstLowering(OutContext, STI, *this);

  StringRef Err;
  if (!STI.getInstrInfo()->verifyInstruction(*MI, Err)) {
    LLVMContext &C = MI->getParent()->getParent()->getFunction().getContext();
    C.emitError("Illegal instruction detected: " + Err);
    MI->print(errs());
  }

  if (MI->isBundle()) {
    const MachineBasicBlock *MBB = MI->getParent();
    MachineBasicBlock::const_instr_iterator I = ++MI->getIterator();
    while (I != MBB->instr_end() && I->isInsideBundle()) {
      emitInstruction(&*I);
      ++I;
    }
  } else {
    MCInst TmpInst;
    MCInstLowering.lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  }
}

const MCExpr *R600AsmPrinter::lowerConstant(const Constant *CV) {
  if (const MCExpr *E = lowerAddrSpaceCast(TM, CV, OutContext))
    return E;
  return AsmPrinter::lowerConstant(CV);
}
