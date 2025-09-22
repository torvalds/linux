//===-- M68kMCInstLower.cpp - M68k MachineInstr to MCInst -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains code to lower M68k MachineInstrs to their
/// corresponding MCInst records.
///
//===----------------------------------------------------------------------===//

#include "M68kMCInstLower.h"

#include "M68kAsmPrinter.h"
#include "M68kInstrInfo.h"

#include "MCTargetDesc/M68kBaseInfo.h"

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-mc-inst-lower"

M68kMCInstLower::M68kMCInstLower(MachineFunction &MF, M68kAsmPrinter &AP)
    : Ctx(MF.getContext()), MF(MF), TM(MF.getTarget()), MAI(*TM.getMCAsmInfo()),
      AsmPrinter(AP) {}

MCSymbol *
M68kMCInstLower::GetSymbolFromOperand(const MachineOperand &MO) const {
  assert((MO.isGlobal() || MO.isSymbol() || MO.isMBB()) &&
         "Isn't a symbol reference");

  const auto &TT = TM.getTargetTriple();
  if (MO.isGlobal() && TT.isOSBinFormatELF())
    return AsmPrinter.getSymbolPreferLocal(*MO.getGlobal());

  const DataLayout &DL = MF.getDataLayout();

  MCSymbol *Sym = nullptr;
  SmallString<128> Name;
  StringRef Suffix;

  if (!Suffix.empty())
    Name += DL.getPrivateGlobalPrefix();

  if (MO.isGlobal()) {
    const GlobalValue *GV = MO.getGlobal();
    AsmPrinter.getNameWithPrefix(Name, GV);
  } else if (MO.isSymbol()) {
    Mangler::getNameWithPrefix(Name, MO.getSymbolName(), DL);
  } else if (MO.isMBB()) {
    assert(Suffix.empty());
    Sym = MO.getMBB()->getSymbol();
  }

  Name += Suffix;
  if (!Sym)
    Sym = Ctx.getOrCreateSymbol(Name);

  return Sym;
}

MCOperand M68kMCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                              MCSymbol *Sym) const {
  // FIXME We would like an efficient form for this, so we don't have to do a
  // lot of extra uniquing. This fixme is originally from X86
  const MCExpr *Expr = nullptr;
  MCSymbolRefExpr::VariantKind RefKind = MCSymbolRefExpr::VK_None;

  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on GV operand");
  case M68kII::MO_NO_FLAG:
  case M68kII::MO_ABSOLUTE_ADDRESS:
  case M68kII::MO_PC_RELATIVE_ADDRESS:
    break;
  case M68kII::MO_GOTPCREL:
    RefKind = MCSymbolRefExpr::VK_GOTPCREL;
    break;
  case M68kII::MO_GOT:
    RefKind = MCSymbolRefExpr::VK_GOT;
    break;
  case M68kII::MO_GOTOFF:
    RefKind = MCSymbolRefExpr::VK_GOTOFF;
    break;
  case M68kII::MO_PLT:
    RefKind = MCSymbolRefExpr::VK_PLT;
    break;
  case M68kII::MO_TLSGD:
    RefKind = MCSymbolRefExpr::VK_TLSGD;
    break;
  case M68kII::MO_TLSLD:
    RefKind = MCSymbolRefExpr::VK_TLSLD;
    break;
  case M68kII::MO_TLSLDM:
    RefKind = MCSymbolRefExpr::VK_TLSLDM;
    break;
  case M68kII::MO_TLSIE:
    RefKind = MCSymbolRefExpr::VK_GOTTPOFF;
    break;
  case M68kII::MO_TLSLE:
    RefKind = MCSymbolRefExpr::VK_TPOFF;
    break;
  }

  if (!Expr) {
    Expr = MCSymbolRefExpr::create(Sym, RefKind, Ctx);
  }

  if (!MO.isJTI() && !MO.isMBB() && MO.getOffset()) {
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  }

  return MCOperand::createExpr(Expr);
}

std::optional<MCOperand>
M68kMCInstLower::LowerOperand(const MachineInstr *MI,
                              const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    // Ignore all implicit register operands.
    if (MO.isImplicit())
      return std::nullopt;
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
    return LowerSymbolOperand(MO, GetSymbolFromOperand(MO));
  case MachineOperand::MO_MCSymbol:
    return LowerSymbolOperand(MO, MO.getMCSymbol());
  case MachineOperand::MO_JumpTableIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetJTISymbol(MO.getIndex()));
  case MachineOperand::MO_ConstantPoolIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetCPISymbol(MO.getIndex()));
  case MachineOperand::MO_BlockAddress:
    return LowerSymbolOperand(
        MO, AsmPrinter.GetBlockAddressSymbol(MO.getBlockAddress()));
  case MachineOperand::MO_RegisterMask:
    // Ignore call clobbers.
    return std::nullopt;
  }
}

void M68kMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  unsigned Opcode = MI->getOpcode();
  OutMI.setOpcode(Opcode);

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    std::optional<MCOperand> MCOp = LowerOperand(MI, MO);

    if (MCOp.has_value() && MCOp.value().isValid())
      OutMI.addOperand(MCOp.value());
  }

  // TAILJMPj, TAILJMPq - Lower to the correct jump instructions.
  if (Opcode == M68k::TAILJMPj || Opcode == M68k::TAILJMPq) {
    assert(OutMI.getNumOperands() == 1 && "Unexpected number of operands");
    switch (Opcode) {
    case M68k::TAILJMPj:
      Opcode = M68k::JMP32j;
      break;
    case M68k::TAILJMPq:
      Opcode = M68k::BRA8;
      break;
    }
    OutMI.setOpcode(Opcode);
  }
}
