//===- LoongArchInstPrinter.cpp - Convert LoongArch MCInst to asm syntax --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an LoongArch MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "LoongArchInstPrinter.h"
#include "LoongArchBaseInfo.h"
#include "LoongArchMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

#define DEBUG_TYPE "loongarch-asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "LoongArchGenAsmWriter.inc"

static cl::opt<bool>
    NumericReg("loongarch-numeric-reg",
               cl::desc("Print numeric register names rather than the ABI "
                        "names (such as $r0 instead of $zero)"),
               cl::init(false), cl::Hidden);

// The command-line flag above is used by llvm-mc and llc. It can be used by
// `llvm-objdump`, but we override the value here to handle options passed to
// `llvm-objdump` with `-M` (which matches GNU objdump). There did not seem to
// be an easier way to allow these options in all these tools, without doing it
// this way.
bool LoongArchInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "numeric") {
    NumericReg = true;
    return true;
  }

  return false;
}

void LoongArchInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                     StringRef Annot,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  if (!printAliasInstr(MI, Address, STI, O))
    printInstruction(MI, Address, STI, O);
  printAnnotation(O, Annot);
}

void LoongArchInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  O << '$' << getRegisterName(Reg);
}

void LoongArchInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    O << MO.getImm();
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MO.getExpr()->print(O, &MAI);
}

void LoongArchInstPrinter::printAtomicMemOp(const MCInst *MI, unsigned OpNo,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  assert(MO.isReg() && "printAtomicMemOp can only print register operands");
  printRegName(O, MO.getReg());
}

const char *LoongArchInstPrinter::getRegisterName(MCRegister Reg) {
  // Default print reg alias name
  return getRegisterName(Reg, NumericReg ? LoongArch::NoRegAltName
                                         : LoongArch::RegAliasName);
}
