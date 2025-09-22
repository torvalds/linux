//===- HexagonMCInstLower.cpp - Convert Hexagon MachineInstr to an MCInst -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Hexagon MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonAsmPrinter.h"
#include "MCTargetDesc/HexagonMCExpr.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

namespace llvm {

void HexagonLowerToMC(const MCInstrInfo &MCII, const MachineInstr *MI,
                      MCInst &MCB, HexagonAsmPrinter &AP);

} // end namespace llvm

static MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol,
                              HexagonAsmPrinter &Printer, bool MustExtend) {
  MCContext &MC = Printer.OutContext;
  const MCExpr *ME;

  // Populate the relocation type based on Hexagon target flags
  // set on an operand
  MCSymbolRefExpr::VariantKind RelocationType;
  switch (MO.getTargetFlags() & ~HexagonII::HMOTF_ConstExtended) {
  default:
    RelocationType = MCSymbolRefExpr::VK_None;
    break;
  case HexagonII::MO_PCREL:
    RelocationType = MCSymbolRefExpr::VK_PCREL;
    break;
  case HexagonII::MO_GOT:
    RelocationType = MCSymbolRefExpr::VK_GOT;
    break;
  case HexagonII::MO_LO16:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_LO16;
    break;
  case HexagonII::MO_HI16:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_HI16;
    break;
  case HexagonII::MO_GPREL:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_GPREL;
    break;
  case HexagonII::MO_GDGOT:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_GD_GOT;
    break;
  case HexagonII::MO_GDPLT:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_GD_PLT;
    break;
  case HexagonII::MO_IE:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_IE;
    break;
  case HexagonII::MO_IEGOT:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_IE_GOT;
    break;
  case HexagonII::MO_TPREL:
    RelocationType = MCSymbolRefExpr::VK_TPREL;
    break;
  }

  ME = MCSymbolRefExpr::create(Symbol, RelocationType, MC);

  if (!MO.isJTI() && MO.getOffset())
    ME = MCBinaryExpr::createAdd(ME, MCConstantExpr::create(MO.getOffset(), MC),
                                 MC);

  ME = HexagonMCExpr::create(ME, MC);
  HexagonMCInstrInfo::setMustExtend(*ME, MustExtend);
  return MCOperand::createExpr(ME);
}

// Create an MCInst from a MachineInstr
void llvm::HexagonLowerToMC(const MCInstrInfo &MCII, const MachineInstr *MI,
                            MCInst &MCB, HexagonAsmPrinter &AP) {
  if (MI->getOpcode() == Hexagon::ENDLOOP0) {
    HexagonMCInstrInfo::setInnerLoop(MCB);
    return;
  }
  if (MI->getOpcode() == Hexagon::ENDLOOP1) {
    HexagonMCInstrInfo::setOuterLoop(MCB);
    return;
  }
  if (MI->getOpcode() == Hexagon::PATCHABLE_FUNCTION_ENTER) {
    AP.EmitSled(*MI, HexagonAsmPrinter::SledKind::FUNCTION_ENTER);
    return;
  }
  if (MI->getOpcode() == Hexagon::PATCHABLE_FUNCTION_EXIT) {
    AP.EmitSled(*MI, HexagonAsmPrinter::SledKind::FUNCTION_EXIT);
    return;
  }
  if (MI->getOpcode() == Hexagon::PATCHABLE_TAIL_CALL) {
    AP.EmitSled(*MI, HexagonAsmPrinter::SledKind::TAIL_CALL);
    return;
  }

  MCInst *MCI = AP.OutContext.createMCInst();
  MCI->setOpcode(MI->getOpcode());
  assert(MCI->getOpcode() == static_cast<unsigned>(MI->getOpcode()) &&
         "MCI opcode should have been set on construction");

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCO;
    bool MustExtend = MO.getTargetFlags() & HexagonII::HMOTF_ConstExtended;

    switch (MO.getType()) {
    default:
      MI->print(errs());
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_RegisterMask:
      continue;
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands.
      if (MO.isImplicit())
        continue;
      MCO = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_FPImmediate: {
      APFloat Val = MO.getFPImm()->getValueAPF();
      // FP immediates are used only when setting GPRs, so they may be dealt
      // with like regular immediates from this point on.
      auto Expr = HexagonMCExpr::create(
          MCConstantExpr::create(*Val.bitcastToAPInt().getRawData(),
                                 AP.OutContext),
          AP.OutContext);
      HexagonMCInstrInfo::setMustExtend(*Expr, MustExtend);
      MCO = MCOperand::createExpr(Expr);
      break;
    }
    case MachineOperand::MO_Immediate: {
      auto Expr = HexagonMCExpr::create(
          MCConstantExpr::create(MO.getImm(), AP.OutContext), AP.OutContext);
      HexagonMCInstrInfo::setMustExtend(*Expr, MustExtend);
      MCO = MCOperand::createExpr(Expr);
      break;
    }
    case MachineOperand::MO_MachineBasicBlock: {
      MCExpr const *Expr = MCSymbolRefExpr::create(MO.getMBB()->getSymbol(),
                                                   AP.OutContext);
      Expr = HexagonMCExpr::create(Expr, AP.OutContext);
      HexagonMCInstrInfo::setMustExtend(*Expr, MustExtend);
      MCO = MCOperand::createExpr(Expr);
      break;
    }
    case MachineOperand::MO_GlobalAddress:
      MCO = GetSymbolRef(MO, AP.getSymbol(MO.getGlobal()), AP, MustExtend);
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCO = GetSymbolRef(MO, AP.GetExternalSymbolSymbol(MO.getSymbolName()),
                         AP, MustExtend);
      break;
    case MachineOperand::MO_JumpTableIndex:
      MCO = GetSymbolRef(MO, AP.GetJTISymbol(MO.getIndex()), AP, MustExtend);
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCO = GetSymbolRef(MO, AP.GetCPISymbol(MO.getIndex()), AP, MustExtend);
      break;
    case MachineOperand::MO_BlockAddress:
      MCO = GetSymbolRef(MO, AP.GetBlockAddressSymbol(MO.getBlockAddress()), AP,
                         MustExtend);
      break;
    }

    MCI->addOperand(MCO);
  }
  AP.HexagonProcessInstruction(*MCI, *MI);
  HexagonMCInstrInfo::extendIfNeeded(AP.OutContext, MCII, MCB, *MCI);
  MCB.addOperand(MCOperand::createInst(MCI));
}
