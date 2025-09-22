//===-- CSKYMCInstLower.cpp - Convert CSKY MachineInstr to an MCInst --------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower CSKY MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "CSKYMCInstLower.h"
#include "MCTargetDesc/CSKYBaseInfo.h"
#include "MCTargetDesc/CSKYMCExpr.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCExpr.h"

#define DEBUG_TYPE "csky-mcinst-lower"

using namespace llvm;

CSKYMCInstLower::CSKYMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
    : Ctx(Ctx), Printer(Printer) {}

void CSKYMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    if (lowerOperand(MO, MCOp))
      OutMI.addOperand(MCOp);
  }
}

MCOperand CSKYMCInstLower::lowerSymbolOperand(const MachineOperand &MO,
                                              MCSymbol *Sym) const {
  CSKYMCExpr::VariantKind Kind;
  MCContext &Ctx = Printer.OutContext;

  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag.");
  case CSKYII::MO_None:
    Kind = CSKYMCExpr::VK_CSKY_None;
    break;
  case CSKYII::MO_GOT32:
    Kind = CSKYMCExpr::VK_CSKY_GOT;
    break;
  case CSKYII::MO_GOTOFF:
    Kind = CSKYMCExpr::VK_CSKY_GOTOFF;
    break;
  case CSKYII::MO_ADDR32:
    Kind = CSKYMCExpr::VK_CSKY_ADDR;
    break;
  case CSKYII::MO_PLT32:
    Kind = CSKYMCExpr::VK_CSKY_PLT;
    break;
  case CSKYII::MO_ADDR_HI16:
    Kind = CSKYMCExpr::VK_CSKY_ADDR_HI16;
    break;
  case CSKYII::MO_ADDR_LO16:
    Kind = CSKYMCExpr::VK_CSKY_ADDR_LO16;
    break;
  }
  const MCExpr *ME =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, Ctx);

  if (Kind != CSKYMCExpr::VK_CSKY_None)
    ME = CSKYMCExpr::create(ME, Kind, Ctx);

  return MCOperand::createExpr(ME);
}

bool CSKYMCInstLower::lowerOperand(const MachineOperand &MO,
                                   MCOperand &MCOp) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_RegisterMask:
    break;
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::createImm(MO.getImm());
    break;
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return false;
    MCOp = MCOperand::createReg(MO.getReg());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MCOp = MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
    break;
  case MachineOperand::MO_GlobalAddress:
    MCOp = lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal()));
    break;
  case MachineOperand::MO_BlockAddress:
    MCOp = lowerSymbolOperand(
        MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress()));
    break;
  case MachineOperand::MO_ExternalSymbol:
    MCOp = lowerSymbolOperand(
        MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName()));
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    MCOp = lowerSymbolOperand(MO, Printer.GetCPISymbol(MO.getIndex()));
    break;
  case MachineOperand::MO_JumpTableIndex:
    MCOp = lowerSymbolOperand(MO, Printer.GetJTISymbol(MO.getIndex()));
    break;
  case MachineOperand::MO_MCSymbol:
    MCOp = lowerSymbolOperand(MO, MO.getMCSymbol());
    break;
  }
  return true;
}
