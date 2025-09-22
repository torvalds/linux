//===-- M68kInstPrinter.cpp - Convert M68k MCInst to asm --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definitions for an M68k MCInst printer.
///
//===----------------------------------------------------------------------===//

// TODO Conform with all supported Motorola ASM syntax
// Motorola's assembly has several syntax variants, especially on
// addressing modes.
// For example, you can write pc indirect w/ displacement as
// `x(%pc)`, where `x` is the displacement imm, or `(x,%pc)`.
// Currently we're picking the variant that is different from
// GCC, albeit being recognizable by GNU AS.
// Not sure what is the impact now (e.g. some syntax might
// not be recognized by some old consoles' toolchains, in which
// case we can not use our integrated assembler), but either way,
// it will be great to support all of the variants in the future.

#include "M68kInstPrinter.h"
#include "M68kBaseInfo.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#define PRINT_ALIAS_INSTR
#include "M68kGenAsmWriter.inc"

void M68kInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) const {
  OS << "%" << getRegisterName(Reg);
}

void M68kInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  if (!printAliasInstr(MI, Address, O))
    printInstruction(MI, Address, O);

  printAnnotation(O, Annot);
}

void M68kInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    printImmediate(MI, OpNo, O);
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MO.getExpr()->print(O, &MAI);
}

void M68kInstPrinter::printImmediate(const MCInst *MI, unsigned opNum,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(opNum);
  if (MO.isImm())
    O << '#' << MO.getImm();
  else if (MO.isExpr()) {
    O << '#';
    MO.getExpr()->print(O, &MAI);
  } else
    llvm_unreachable("Unknown immediate kind");
}

void M68kInstPrinter::printMoveMask(const MCInst *MI, unsigned opNum,
                                    raw_ostream &O) {
  unsigned Mask = MI->getOperand(opNum).getImm();
  assert((Mask & 0xFFFF) == Mask && "Mask is always 16 bits");

  // A move mask is splitted into two parts:
  // bits 0 ~ 7  correspond to D0 ~ D7 regs
  // bits 8 ~ 15 correspond to A0 ~ A7 regs
  //
  // In the assembly syntax, we want to use a dash to replace
  // a continuous range of registers. For example, if the bit
  // mask is 0b101110, we want to print "D1-D3,D5" instead of
  // "D1,D2,D3,D4,D5".
  //
  // However, we don't want a dash to cross between data registers
  // and address registers (i.e. there shouldn't be a dash crossing
  // bit 7 and 8) since that is not really intuitive. So we simply
  // print the data register part (bit 0~7) and address register part
  // separately.
  uint8_t HalfMask;
  unsigned Reg;
  for (int s = 0; s < 16; s += 8) {
    HalfMask = (Mask >> s) & 0xFF;
    // Print separation comma only if
    // both data & register parts have bit(s) set
    if (s != 0 && (Mask & 0xFF) && HalfMask)
      O << '/';

    for (int i = 0; HalfMask; ++i) {
      if ((HalfMask >> i) & 0b1) {
        HalfMask ^= 0b1 << i;
        Reg = M68kII::getMaskedSpillRegister(i + s);
        printRegName(O, Reg);

        int j = i;
        while ((HalfMask >> (j + 1)) & 0b1)
          HalfMask ^= 0b1 << ++j;

        if (j != i) {
          O << '-';
          Reg = M68kII::getMaskedSpillRegister(j + s);
          printRegName(O, Reg);
        }

        i = j;

        if (HalfMask)
          O << '/';
      }
    }
  }
}

void M68kInstPrinter::printDisp(const MCInst *MI, unsigned opNum,
                                raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(opNum);
  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "Unknown operand kind in printOperand");
  Op.getExpr()->print(O, &MAI);
}

// NOTE forcing (W,L) size available since M68020 only
void M68kInstPrinter::printAbsMem(const MCInst *MI, unsigned opNum,
                                  raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(opNum);

  if (MO.isExpr()) {
    MO.getExpr()->print(O, &MAI);
    return;
  }

  assert(MO.isImm() && "absolute memory addressing needs an immediate");
  O << format("$%0" PRIx64, (uint64_t)MO.getImm());
}
