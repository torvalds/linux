//===- SystemZInstPrinter.cpp - Convert SystemZ MCInst to assembly syntax -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZInstPrinter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "SystemZGenAsmWriter.inc"

void SystemZInstPrinter::printAddress(const MCAsmInfo *MAI, MCRegister Base,
                                      const MCOperand &DispMO, MCRegister Index,
                                      raw_ostream &O) {
  printOperand(DispMO, MAI, O);
  if (Base || Index) {
    O << '(';
    if (Index) {
      printFormattedRegName(MAI, Index, O);
      O << ',';
    }
    if (Base)
      printFormattedRegName(MAI, Base, O);
    else
      O << '0';
    O << ')';
  }
}

void SystemZInstPrinter::printOperand(const MCOperand &MO, const MCAsmInfo *MAI,
                                      raw_ostream &O) {
  if (MO.isReg()) {
    if (!MO.getReg())
      O << '0';
    else
      printFormattedRegName(MAI, MO.getReg(), O);
  }
  else if (MO.isImm())
    markup(O, Markup::Immediate) << MO.getImm();
  else if (MO.isExpr())
    MO.getExpr()->print(O, MAI);
  else
    llvm_unreachable("Invalid operand");
}

void SystemZInstPrinter::printFormattedRegName(const MCAsmInfo *MAI,
                                               MCRegister Reg,
                                               raw_ostream &O) const {
  const char *RegName = getRegisterName(Reg);
  if (MAI->getAssemblerDialect() == AD_HLASM) {
    // Skip register prefix so that only register number is left
    assert(isalpha(RegName[0]) && isdigit(RegName[1]));
    markup(O, Markup::Register) << (RegName + 1);
  } else
    markup(O, Markup::Register) << '%' << RegName;
}

void SystemZInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) const {
  printFormattedRegName(&MAI, Reg, O);
}

void SystemZInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

template <unsigned N>
void SystemZInstPrinter::printUImmOperand(const MCInst *MI, int OpNum,
                                          raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.isExpr()) {
    O << *MO.getExpr();
    return;
  }
  uint64_t Value = static_cast<uint64_t>(MO.getImm());
  assert(isUInt<N>(Value) && "Invalid uimm argument");
  markup(O, Markup::Immediate) << Value;
}

template <unsigned N>
void SystemZInstPrinter::printSImmOperand(const MCInst *MI, int OpNum,
                                          raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.isExpr()) {
    O << *MO.getExpr();
    return;
  }
  int64_t Value = MI->getOperand(OpNum).getImm();
  assert(isInt<N>(Value) && "Invalid simm argument");
  markup(O, Markup::Immediate) << Value;
}

void SystemZInstPrinter::printU1ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<1>(MI, OpNum, O);
}

void SystemZInstPrinter::printU2ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<2>(MI, OpNum, O);
}

void SystemZInstPrinter::printU3ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<3>(MI, OpNum, O);
}

void SystemZInstPrinter::printU4ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<4>(MI, OpNum, O);
}

void SystemZInstPrinter::printS8ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printSImmOperand<8>(MI, OpNum, O);
}

void SystemZInstPrinter::printU8ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<8>(MI, OpNum, O);
}

void SystemZInstPrinter::printU12ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<12>(MI, OpNum, O);
}

void SystemZInstPrinter::printS16ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printSImmOperand<16>(MI, OpNum, O);
}

void SystemZInstPrinter::printU16ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<16>(MI, OpNum, O);
}

void SystemZInstPrinter::printS32ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printSImmOperand<32>(MI, OpNum, O);
}

void SystemZInstPrinter::printU32ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<32>(MI, OpNum, O);
}

void SystemZInstPrinter::printU48ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<48>(MI, OpNum, O);
}

void SystemZInstPrinter::printPCRelOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.isImm()) {
    WithMarkup M = markup(O, Markup::Immediate);
    O << "0x";
    O.write_hex(MO.getImm());
  } else
    MO.getExpr()->print(O, &MAI);
}

void SystemZInstPrinter::printPCRelTLSOperand(const MCInst *MI,
                                              uint64_t Address, int OpNum,
                                              raw_ostream &O) {
  // Output the PC-relative operand.
  printPCRelOperand(MI, OpNum, O);

  // Output the TLS marker if present.
  if ((unsigned)OpNum + 1 < MI->getNumOperands()) {
    const MCOperand &MO = MI->getOperand(OpNum + 1);
    const MCSymbolRefExpr &refExp = cast<MCSymbolRefExpr>(*MO.getExpr());
    switch (refExp.getKind()) {
      case MCSymbolRefExpr::VK_TLSGD:
        O << ":tls_gdcall:";
        break;
      case MCSymbolRefExpr::VK_TLSLDM:
        O << ":tls_ldcall:";
        break;
      default:
        llvm_unreachable("Unexpected symbol kind");
    }
    O << refExp.getSymbol().getName();
  }
}

void SystemZInstPrinter::printOperand(const MCInst *MI, int OpNum,
                                      raw_ostream &O) {
  printOperand(MI->getOperand(OpNum), &MAI, O);
}

void SystemZInstPrinter::printBDAddrOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printAddress(&MAI, MI->getOperand(OpNum).getReg(), MI->getOperand(OpNum + 1),
               0, O);
}

void SystemZInstPrinter::printBDXAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  printAddress(&MAI, MI->getOperand(OpNum).getReg(), MI->getOperand(OpNum + 1),
               MI->getOperand(OpNum + 2).getReg(), O);
}

void SystemZInstPrinter::printBDLAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  unsigned Base = MI->getOperand(OpNum).getReg();
  const MCOperand &DispMO = MI->getOperand(OpNum + 1);
  uint64_t Length = MI->getOperand(OpNum + 2).getImm();
  printOperand(DispMO, &MAI, O);
  O << '(' << Length;
  if (Base) {
    O << ",";
    printRegName(O, Base);
  }
  O << ')';
}

void SystemZInstPrinter::printBDRAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  unsigned Base = MI->getOperand(OpNum).getReg();
  const MCOperand &DispMO = MI->getOperand(OpNum + 1);
  unsigned Length = MI->getOperand(OpNum + 2).getReg();
  printOperand(DispMO, &MAI, O);
  O << "(";
  printRegName(O, Length);
  if (Base) {
    O << ",";
    printRegName(O, Base);
  }
  O << ')';
}

void SystemZInstPrinter::printBDVAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  printAddress(&MAI, MI->getOperand(OpNum).getReg(), MI->getOperand(OpNum + 1),
               MI->getOperand(OpNum + 2).getReg(), O);
}

void SystemZInstPrinter::printCond4Operand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  static const char *const CondNames[] = {
    "o", "h", "nle", "l", "nhe", "lh", "ne",
    "e", "nlh", "he", "nl", "le", "nh", "no"
  };
  uint64_t Imm = MI->getOperand(OpNum).getImm();
  assert(Imm > 0 && Imm < 15 && "Invalid condition");
  O << CondNames[Imm - 1];
}
