//===-- M68kAsmPrinter.cpp - M68k LLVM Assembly Printer ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a printer that converts from our internal representation
/// of machine-dependent LLVM code to GAS-format M68k assembly language.
///
//===----------------------------------------------------------------------===//

// TODO Conform to Motorola ASM syntax

#include "M68kAsmPrinter.h"

#include "M68k.h"
#include "M68kMachineFunction.h"
#include "MCTargetDesc/M68kInstPrinter.h"
#include "TargetInfo/M68kTargetInfo.h"

#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-asm-printer"

bool M68kAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  MMFI = MF.getInfo<M68kMachineFunctionInfo>();
  MCInstLowering = std::make_unique<M68kMCInstLower>(MF, *this);
  AsmPrinter::runOnMachineFunction(MF);
  return true;
}

void M68kAsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                  raw_ostream &OS) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    OS << "%" << M68kInstPrinter::getRegisterName(MO.getReg());
    break;
  case MachineOperand::MO_Immediate:
    OS << '#' << MO.getImm();
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(OS, MAI);
    break;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    break;
  case MachineOperand::MO_BlockAddress:
    GetBlockAddressSymbol(MO.getBlockAddress())->print(OS, MAI);
    break;
  case MachineOperand::MO_ConstantPoolIndex: {
    const DataLayout &DL = getDataLayout();
    OS << DL.getPrivateGlobalPrefix() << "CPI" << getFunctionNumber() << '_'
       << MO.getIndex();
    break;
  }
  default:
    llvm_unreachable("not implemented");
  }
}

bool M68kAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS) {
  // Print the operand if there is no operand modifier.
  if (!ExtraCode || !ExtraCode[0]) {
    printOperand(MI, OpNo, OS);
    return false;
  }

  // Fallback to the default implementation.
  return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
}

void M68kAsmPrinter::printDisp(const MachineInstr *MI, unsigned opNum,
                               raw_ostream &O) {
  // Print immediate displacement without the '#' predix
  const MachineOperand &Op = MI->getOperand(opNum);
  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }
  // Displacement is relocatable, so we're pretty permissive about what
  // can be put here.
  printOperand(MI, opNum, O);
}

void M68kAsmPrinter::printAbsMem(const MachineInstr *MI, unsigned OpNum,
                                 raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  if (MO.isImm())
    O << format("$%0" PRIx64, (uint64_t)MO.getImm());
  else
    PrintAsmMemoryOperand(MI, OpNum, nullptr, O);
}

bool M68kAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                           unsigned OpNo, const char *ExtraCode,
                                           raw_ostream &OS) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    // Immediate value that goes here is the addressing mode kind we set
    // in M68kDAGToDAGISel::SelectInlineAsmMemoryOperand.
    using namespace M68k;
    // Skip the addressing mode kind operand.
    ++OpNo;
    // Decode MemAddrModeKind.
    switch (static_cast<MemAddrModeKind>(MO.getImm())) {
    case MemAddrModeKind::j:
      printARIMem(MI, OpNo, OS);
      break;
    case MemAddrModeKind::o:
      printARIPIMem(MI, OpNo, OS);
      break;
    case MemAddrModeKind::e:
      printARIPDMem(MI, OpNo, OS);
      break;
    case MemAddrModeKind::p:
      printARIDMem(MI, OpNo, OS);
      break;
    case MemAddrModeKind::f:
    case MemAddrModeKind::F:
      printARIIMem(MI, OpNo, OS);
      break;
    case MemAddrModeKind::k:
      printPCIMem(MI, 0, OpNo, OS);
      break;
    case MemAddrModeKind::q:
      printPCDMem(MI, 0, OpNo, OS);
      break;
    case MemAddrModeKind::b:
      printAbsMem(MI, OpNo, OS);
      break;
    default:
      llvm_unreachable("Unrecognized memory addressing mode");
    }
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  case MachineOperand::MO_BlockAddress:
    GetBlockAddressSymbol(MO.getBlockAddress())->print(OS, MAI);
    return false;
  case MachineOperand::MO_Register:
    // This is a special case where it is treated as a memory reference, with
    // the register holding the address value. Thus, we print it as ARI here.
    if (M68kII::isAddressRegister(MO.getReg())) {
      printARIMem(MI, OpNo, OS);
      return false;
    }
    break;
  default:
    break;
  }
  return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, OS);
}

void M68kAsmPrinter::emitInstruction(const MachineInstr *MI) {
  M68k_MC::verifyInstructionPredicates(MI->getOpcode(),
                                       getSubtargetInfo().getFeatureBits());

  switch (MI->getOpcode()) {
  default: {
    if (MI->isPseudo()) {
      LLVM_DEBUG(dbgs() << "Pseudo opcode(" << MI->getOpcode()
                        << ") found in EmitInstruction()\n");
      llvm_unreachable("Cannot proceed");
    }
    break;
  }
  case M68k::TAILJMPj:
  case M68k::TAILJMPq:
    // Lower these as normal, but add some comments.
    OutStreamer->AddComment("TAILCALL");
    break;
  }

  MCInst TmpInst0;
  MCInstLowering->Lower(MI, TmpInst0);
  OutStreamer->emitInstruction(TmpInst0, getSubtargetInfo());
}

void M68kAsmPrinter::emitFunctionBodyStart() {}

void M68kAsmPrinter::emitFunctionBodyEnd() {}

void M68kAsmPrinter::emitStartOfAsmFile(Module &M) {
  OutStreamer->emitSyntaxDirective();
}

void M68kAsmPrinter::emitEndOfAsmFile(Module &M) {}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kAsmPrinter() {
  RegisterAsmPrinter<M68kAsmPrinter> X(getTheM68kTarget());
}
