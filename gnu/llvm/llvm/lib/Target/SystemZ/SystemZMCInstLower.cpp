//===-- SystemZMCInstLower.cpp - Lower MachineInstr to MCInst -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZMCInstLower.h"
#include "SystemZAsmPrinter.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

// Return the VK_* enumeration for MachineOperand target flags Flags.
static MCSymbolRefExpr::VariantKind getVariantKind(unsigned Flags) {
  switch (Flags & SystemZII::MO_SYMBOL_MODIFIER) {
    case 0:
      return MCSymbolRefExpr::VK_None;
    case SystemZII::MO_GOT:
      return MCSymbolRefExpr::VK_GOT;
    case SystemZII::MO_INDNTPOFF:
      return MCSymbolRefExpr::VK_INDNTPOFF;
  }
  llvm_unreachable("Unrecognised MO_ACCESS_MODEL");
}

SystemZMCInstLower::SystemZMCInstLower(MCContext &ctx,
                                       SystemZAsmPrinter &asmprinter)
  : Ctx(ctx), AsmPrinter(asmprinter) {}

const MCExpr *
SystemZMCInstLower::getExpr(const MachineOperand &MO,
                            MCSymbolRefExpr::VariantKind Kind) const {
  const MCSymbol *Symbol;
  bool HasOffset = true;
  switch (MO.getType()) {
  case MachineOperand::MO_MachineBasicBlock:
    Symbol = MO.getMBB()->getSymbol();
    HasOffset = false;
    break;

  case MachineOperand::MO_GlobalAddress:
    Symbol = AsmPrinter.getSymbol(MO.getGlobal());
    break;

  case MachineOperand::MO_ExternalSymbol:
    Symbol = AsmPrinter.GetExternalSymbolSymbol(MO.getSymbolName());
    break;

  case MachineOperand::MO_JumpTableIndex:
    Symbol = AsmPrinter.GetJTISymbol(MO.getIndex());
    HasOffset = false;
    break;

  case MachineOperand::MO_ConstantPoolIndex:
    Symbol = AsmPrinter.GetCPISymbol(MO.getIndex());
    break;

  case MachineOperand::MO_BlockAddress:
    Symbol = AsmPrinter.GetBlockAddressSymbol(MO.getBlockAddress());
    break;

  default:
    llvm_unreachable("unknown operand type");
  }
  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, Kind, Ctx);
  if (HasOffset)
    if (int64_t Offset = MO.getOffset()) {
      const MCExpr *OffsetExpr = MCConstantExpr::create(Offset, Ctx);
      Expr = MCBinaryExpr::createAdd(Expr, OffsetExpr, Ctx);
    }
  return Expr;
}

MCOperand SystemZMCInstLower::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    return MCOperand::createReg(MO.getReg());

  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());

  default: {
    MCSymbolRefExpr::VariantKind Kind = getVariantKind(MO.getTargetFlags());
    return MCOperand::createExpr(getExpr(MO, Kind));
  }
  }
}

void SystemZMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands())
    // Ignore all implicit register operands.
    if (!MO.isReg() || !MO.isImplicit())
      OutMI.addOperand(lowerOperand(MO));
}
