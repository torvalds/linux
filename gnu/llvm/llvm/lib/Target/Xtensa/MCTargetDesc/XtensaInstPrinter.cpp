//===- XtensaInstPrinter.cpp - Convert Xtensa MCInst to asm syntax --------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an Xtensa MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "XtensaInstPrinter.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "XtensaGenAsmWriter.inc"

static void printExpr(const MCExpr *Expr, raw_ostream &OS) {
  int Offset = 0;
  const MCSymbolRefExpr *SRE;

  if (!(SRE = cast<MCSymbolRefExpr>(Expr)))
    assert(false && "Unexpected MCExpr type.");

  MCSymbolRefExpr::VariantKind Kind = SRE->getKind();

  switch (Kind) {
  case MCSymbolRefExpr::VK_None:
    break;
  // TODO
  default:
    report_fatal_error("Invalid kind!");
  }

  OS << SRE->getSymbol();

  if (Offset) {
    if (Offset > 0)
      OS << '+';
    OS << Offset;
  }

  if (Kind != MCSymbolRefExpr::VK_None)
    OS << ')';
}

void XtensaInstPrinter::printOperand(const MCOperand &MC, raw_ostream &O) {
  if (MC.isReg())
    O << getRegisterName(MC.getReg());
  else if (MC.isImm())
    O << MC.getImm();
  else if (MC.isExpr())
    printExpr(MC.getExpr(), O);
  else
    report_fatal_error("Invalid operand");
}

void XtensaInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void XtensaInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  O << getRegisterName(Reg);
}

void XtensaInstPrinter::printOperand(const MCInst *MI, int OpNum,
                                     raw_ostream &O) {
  printOperand(MI->getOperand(OpNum), O);
}

void XtensaInstPrinter::printMemOperand(const MCInst *MI, int OpNum,
                                        raw_ostream &OS) {
  OS << getRegisterName(MI->getOperand(OpNum).getReg());
  OS << ", ";
  printOperand(MI, OpNum + 1, OS);
}

void XtensaInstPrinter::printBranchTarget(const MCInst *MI, int OpNum,
                                          raw_ostream &OS) {
  const MCOperand &MC = MI->getOperand(OpNum);
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Val = MC.getImm() + 4;
    OS << ". ";
    if (Val > 0)
      OS << '+';
    OS << Val;
  } else if (MC.isExpr())
    MC.getExpr()->print(OS, &MAI, true);
  else
    llvm_unreachable("Invalid operand");
}

void XtensaInstPrinter::printJumpTarget(const MCInst *MI, int OpNum,
                                        raw_ostream &OS) {
  const MCOperand &MC = MI->getOperand(OpNum);
  if (MC.isImm()) {
    int64_t Val = MC.getImm() + 4;
    OS << ". ";
    if (Val > 0)
      OS << '+';
    OS << Val;
  } else if (MC.isExpr())
    MC.getExpr()->print(OS, &MAI, true);
  else
    llvm_unreachable("Invalid operand");
  ;
}

void XtensaInstPrinter::printCallOperand(const MCInst *MI, int OpNum,
                                         raw_ostream &OS) {
  const MCOperand &MC = MI->getOperand(OpNum);
  if (MC.isImm()) {
    int64_t Val = MC.getImm() + 4;
    OS << ". ";
    if (Val > 0)
      OS << '+';
    OS << Val;
  } else if (MC.isExpr())
    MC.getExpr()->print(OS, &MAI, true);
  else
    llvm_unreachable("Invalid operand");
}

void XtensaInstPrinter::printL32RTarget(const MCInst *MI, int OpNum,
                                        raw_ostream &O) {
  const MCOperand &MC = MI->getOperand(OpNum);
  if (MC.isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    int64_t InstrOff = Value & 0x3;
    Value -= InstrOff;
    assert((Value >= -262144 && Value <= -4) &&
           "Invalid argument, value must be in ranges [-262144,-4]");
    Value += ((InstrOff + 0x3) & 0x4) - InstrOff;
    O << ". ";
    O << Value;
  } else if (MC.isExpr())
    MC.getExpr()->print(O, &MAI, true);
  else
    llvm_unreachable("Invalid operand");
}

void XtensaInstPrinter::printImm8_AsmOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert(isInt<8>(Value) &&
           "Invalid argument, value must be in ranges [-128,127]");
    O << Value;
  } else {
    printOperand(MI, OpNum, O);
  }
}

void XtensaInstPrinter::printImm8_sh8_AsmOperand(const MCInst *MI, int OpNum,
                                                 raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((isInt<16>(Value) && ((Value & 0xFF) == 0)) &&
           "Invalid argument, value must be multiples of 256 in range "
           "[-32768,32512]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printImm12_AsmOperand(const MCInst *MI, int OpNum,
                                              raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= -2048 && Value <= 2047) &&
           "Invalid argument, value must be in ranges [-2048,2047]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printImm12m_AsmOperand(const MCInst *MI, int OpNum,
                                               raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= -2048 && Value <= 2047) &&
           "Invalid argument, value must be in ranges [-2048,2047]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printUimm4_AsmOperand(const MCInst *MI, int OpNum,
                                              raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 0 && Value <= 15) && "Invalid argument");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printUimm5_AsmOperand(const MCInst *MI, int OpNum,
                                              raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 0 && Value <= 31) && "Invalid argument");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printShimm1_31_AsmOperand(const MCInst *MI, int OpNum,
                                                  raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 1 && Value <= 31) &&
           "Invalid argument, value must be in range [1,31]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printImm1_16_AsmOperand(const MCInst *MI, int OpNum,
                                                raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 1 && Value <= 16) &&
           "Invalid argument, value must be in range [1,16]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printOffset8m8_AsmOperand(const MCInst *MI, int OpNum,
                                                  raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 0 && Value <= 255) &&
           "Invalid argument, value must be in range [0,255]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printOffset8m16_AsmOperand(const MCInst *MI, int OpNum,
                                                   raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 0 && Value <= 510 && ((Value & 0x1) == 0)) &&
           "Invalid argument, value must be multiples of two in range [0,510]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printOffset8m32_AsmOperand(const MCInst *MI, int OpNum,
                                                   raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert(
        (Value >= 0 && Value <= 1020 && ((Value & 0x3) == 0)) &&
        "Invalid argument, value must be multiples of four in range [0,1020]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printOffset4m32_AsmOperand(const MCInst *MI, int OpNum,
                                                   raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();
    assert((Value >= 0 && Value <= 60 && ((Value & 0x3) == 0)) &&
           "Invalid argument, value must be multiples of four in range [0,60]");
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printB4const_AsmOperand(const MCInst *MI, int OpNum,
                                                raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();

    switch (Value) {
    case -1:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 12:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      break;
    default:
      assert((0) && "Invalid B4const argument");
    }
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}

void XtensaInstPrinter::printB4constu_AsmOperand(const MCInst *MI, int OpNum,
                                                 raw_ostream &O) {
  if (MI->getOperand(OpNum).isImm()) {
    int64_t Value = MI->getOperand(OpNum).getImm();

    switch (Value) {
    case 32768:
    case 65536:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 12:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      break;
    default:
      assert((0) && "Invalid B4constu argument");
    }
    O << Value;
  } else
    printOperand(MI, OpNum, O);
}
