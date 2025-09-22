//===-- AVRMCInstLower.cpp - Convert AVR MachineInstr to an MCInst --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower AVR MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "AVRMCInstLower.h"
#include "AVRInstrInfo.h"
#include "MCTargetDesc/AVRMCExpr.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

MCOperand
AVRMCInstLower::lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym,
                                   const AVRSubtarget &Subtarget) const {
  unsigned char TF = MO.getTargetFlags();
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);

  bool IsNegated = false;
  if (TF & AVRII::MO_NEG) {
    IsNegated = true;
  }

  if (!MO.isJTI() && MO.getOffset()) {
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  }

  bool IsFunction = MO.isGlobal() && isa<Function>(MO.getGlobal());

  if (TF & AVRII::MO_LO) {
    if (IsFunction) {
      Expr =
          AVRMCExpr::create(Subtarget.hasEIJMPCALL() ? AVRMCExpr::VK_AVR_LO8_GS
                                                     : AVRMCExpr::VK_AVR_PM_LO8,
                            Expr, IsNegated, Ctx);
    } else {
      Expr = AVRMCExpr::create(AVRMCExpr::VK_AVR_LO8, Expr, IsNegated, Ctx);
    }
  } else if (TF & AVRII::MO_HI) {
    if (IsFunction) {
      Expr =
          AVRMCExpr::create(Subtarget.hasEIJMPCALL() ? AVRMCExpr::VK_AVR_HI8_GS
                                                     : AVRMCExpr::VK_AVR_PM_HI8,
                            Expr, IsNegated, Ctx);
    } else {
      Expr = AVRMCExpr::create(AVRMCExpr::VK_AVR_HI8, Expr, IsNegated, Ctx);
    }
  } else if (TF != 0) {
    llvm_unreachable("Unknown target flag on symbol operand");
  }

  return MCOperand::createExpr(Expr);
}

void AVRMCInstLower::lowerInstruction(const MachineInstr &MI,
                                      MCInst &OutMI) const {
  auto &Subtarget = MI.getParent()->getParent()->getSubtarget<AVRSubtarget>();
  OutMI.setOpcode(MI.getOpcode());

  for (MachineOperand const &MO : MI.operands()) {
    MCOperand MCOp;

    switch (MO.getType()) {
    default:
      MI.print(errs());
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands.
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp =
          lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal()), Subtarget);
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = lowerSymbolOperand(
          MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName()), Subtarget);
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_RegisterMask:
      continue;
    case MachineOperand::MO_BlockAddress:
      MCOp = lowerSymbolOperand(
          MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress()), Subtarget);
      break;
    case MachineOperand::MO_JumpTableIndex:
      MCOp = lowerSymbolOperand(MO, Printer.GetJTISymbol(MO.getIndex()),
                                Subtarget);
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCOp = lowerSymbolOperand(MO, Printer.GetCPISymbol(MO.getIndex()),
                                Subtarget);
      break;
    }

    OutMI.addOperand(MCOp);
  }
}

} // end of namespace llvm
