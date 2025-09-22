//===- LoongArchAsmPrinter.cpp - LoongArch LLVM Assembly Printer -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format LoongArch assembly language.
//
//===----------------------------------------------------------------------===//

#include "LoongArchAsmPrinter.h"
#include "LoongArch.h"
#include "LoongArchTargetMachine.h"
#include "MCTargetDesc/LoongArchInstPrinter.h"
#include "TargetInfo/LoongArchTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-asm-printer"

// Simple pseudo-instructions have their lowering (with expansion to real
// instructions) auto-generated.
#include "LoongArchGenMCPseudoLowering.inc"

void LoongArchAsmPrinter::emitInstruction(const MachineInstr *MI) {
  LoongArch_MC::verifyInstructionPredicates(
      MI->getOpcode(), getSubtargetInfo().getFeatureBits());

  // Do any auto-generated pseudo lowerings.
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  switch (MI->getOpcode()) {
  case TargetOpcode::PATCHABLE_FUNCTION_ENTER:
    LowerPATCHABLE_FUNCTION_ENTER(*MI);
    return;
  case TargetOpcode::PATCHABLE_FUNCTION_EXIT:
    LowerPATCHABLE_FUNCTION_EXIT(*MI);
    return;
  case TargetOpcode::PATCHABLE_TAIL_CALL:
    LowerPATCHABLE_TAIL_CALL(*MI);
    return;
  }

  MCInst TmpInst;
  if (!lowerLoongArchMachineInstrToMCInst(MI, TmpInst, *this))
    EmitToStreamer(*OutStreamer, TmpInst);
}

bool LoongArchAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                          const char *ExtraCode,
                                          raw_ostream &OS) {
  // First try the generic code, which knows about modifiers like 'c' and 'n'.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS))
    return false;

  const MachineOperand &MO = MI->getOperand(OpNo);
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      return true; // Unknown modifier.
    case 'z':      // Print $zero register if zero, regular printing otherwise.
      if (MO.isImm() && MO.getImm() == 0) {
        OS << '$' << LoongArchInstPrinter::getRegisterName(LoongArch::R0);
        return false;
      }
      break;
    case 'w': // Print LSX registers.
      if (MO.getReg().id() >= LoongArch::VR0 &&
          MO.getReg().id() <= LoongArch::VR31)
        break;
      // The modifier is 'w' but the operand is not an LSX register; Report an
      // unknown operand error.
      return true;
    case 'u': // Print LASX registers.
      if (MO.getReg().id() >= LoongArch::XR0 &&
          MO.getReg().id() <= LoongArch::XR31)
        break;
      // The modifier is 'u' but the operand is not an LASX register; Report an
      // unknown operand error.
      return true;
      // TODO: handle other extra codes if any.
    }
  }

  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    return false;
  case MachineOperand::MO_Register:
    OS << '$' << LoongArchInstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  default:
    llvm_unreachable("not implemented");
  }

  return true;
}

bool LoongArchAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                                unsigned OpNo,
                                                const char *ExtraCode,
                                                raw_ostream &OS) {
  // TODO: handle extra code.
  if (ExtraCode)
    return true;

  // We only support memory operands like "Base + Offset", where base must be a
  // register, and offset can be a register or an immediate value.
  const MachineOperand &BaseMO = MI->getOperand(OpNo);
  // Base address must be a register.
  if (!BaseMO.isReg())
    return true;
  // Print the base address register.
  OS << "$" << LoongArchInstPrinter::getRegisterName(BaseMO.getReg());
  // Print the offset operand.
  const MachineOperand &OffsetMO = MI->getOperand(OpNo + 1);
  if (OffsetMO.isReg())
    OS << ", $" << LoongArchInstPrinter::getRegisterName(OffsetMO.getReg());
  else if (OffsetMO.isImm())
    OS << ", " << OffsetMO.getImm();
  else
    return true;

  return false;
}

void LoongArchAsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(
    const MachineInstr &MI) {
  const Function &F = MF->getFunction();
  if (F.hasFnAttribute("patchable-function-entry")) {
    unsigned Num;
    if (F.getFnAttribute("patchable-function-entry")
            .getValueAsString()
            .getAsInteger(10, Num))
      return;
    emitNops(Num);
    return;
  }

  emitSled(MI, SledKind::FUNCTION_ENTER);
}

void LoongArchAsmPrinter::LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI) {
  emitSled(MI, SledKind::FUNCTION_EXIT);
}

void LoongArchAsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI) {
  emitSled(MI, SledKind::TAIL_CALL);
}

void LoongArchAsmPrinter::emitSled(const MachineInstr &MI, SledKind Kind) {
  // For loongarch64 we want to emit the following pattern:
  //
  // .Lxray_sled_beginN:
  //   B .Lxray_sled_endN
  //   11 NOPs (44 bytes)
  // .Lxray_sled_endN:
  //
  // We need the extra bytes because at runtime they may be used for the
  // actual pattern defined at compiler-rt/lib/xray/xray_loongarch64.cpp.
  // The count here should be adjusted accordingly if the implementation
  // changes.
  const int8_t NoopsInSledCount = 11;
  OutStreamer->emitCodeAlignment(Align(4), &getSubtargetInfo());
  MCSymbol *BeginOfSled = OutContext.createTempSymbol("xray_sled_begin");
  MCSymbol *EndOfSled = OutContext.createTempSymbol("xray_sled_end");
  OutStreamer->emitLabel(BeginOfSled);
  EmitToStreamer(*OutStreamer,
                 MCInstBuilder(LoongArch::B)
                     .addExpr(MCSymbolRefExpr::create(EndOfSled, OutContext)));
  emitNops(NoopsInSledCount);
  OutStreamer->emitLabel(EndOfSled);
  recordSled(BeginOfSled, MI, Kind, 2);
}

bool LoongArchAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  AsmPrinter::runOnMachineFunction(MF);
  // Emit the XRay table for this function.
  emitXRayTable();
  return true;
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLoongArchAsmPrinter() {
  RegisterAsmPrinter<LoongArchAsmPrinter> X(getTheLoongArch32Target());
  RegisterAsmPrinter<LoongArchAsmPrinter> Y(getTheLoongArch64Target());
}
