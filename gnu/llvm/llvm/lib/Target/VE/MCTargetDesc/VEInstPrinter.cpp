//===-- VEInstPrinter.cpp - Convert VE MCInst to assembly syntax -----------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an VE MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "VEInstPrinter.h"
#include "VE.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ve-asmprinter"

#define GET_INSTRUCTION_NAME
#define PRINT_ALIAS_INSTR
#include "VEGenAsmWriter.inc"

void VEInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) const {
  // Generic registers have identical register name among register classes.
  unsigned AltIdx = VE::AsmName;
  // Misc registers have each own name, so no use alt-names.
  if (MRI.getRegClass(VE::MISCRegClassID).contains(Reg))
    AltIdx = VE::NoRegAltName;
  OS << '%' << getRegisterName(Reg, AltIdx);
}

void VEInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                              StringRef Annot, const MCSubtargetInfo &STI,
                              raw_ostream &OS) {
  if (!printAliasInstr(MI, Address, STI, OS))
    printInstruction(MI, Address, STI, OS);
  printAnnotation(OS, Annot);
}

void VEInstPrinter::printOperand(const MCInst *MI, int OpNum,
                                 const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);

  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    // Expects signed 32bit literals.
    int32_t TruncatedImm = static_cast<int32_t>(MO.getImm());
    O << TruncatedImm;
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MO.getExpr()->print(O, &MAI);
}

void VEInstPrinter::printMemASXOperand(const MCInst *MI, int OpNum,
                                       const MCSubtargetInfo &STI,
                                       raw_ostream &O, const char *Modifier) {
  // If this is an ADD operand, emit it like normal operands.
  if (Modifier && !strcmp(Modifier, "arith")) {
    printOperand(MI, OpNum, STI, O);
    O << ", ";
    printOperand(MI, OpNum + 1, STI, O);
    return;
  }

  if (MI->getOperand(OpNum + 2).isImm() &&
      MI->getOperand(OpNum + 2).getImm() == 0) {
    // don't print "+0"
  } else {
    printOperand(MI, OpNum + 2, STI, O);
  }
  if (MI->getOperand(OpNum + 1).isImm() &&
      MI->getOperand(OpNum + 1).getImm() == 0 &&
      MI->getOperand(OpNum).isImm() && MI->getOperand(OpNum).getImm() == 0) {
    if (MI->getOperand(OpNum + 2).isImm() &&
        MI->getOperand(OpNum + 2).getImm() == 0) {
      O << "0";
    } else {
      // don't print "+0,+0"
    }
  } else {
    O << "(";
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0) {
      // don't print "+0"
    } else {
      printOperand(MI, OpNum + 1, STI, O);
    }
    if (MI->getOperand(OpNum).isImm() && MI->getOperand(OpNum).getImm() == 0) {
      // don't print "+0"
    } else {
      O << ", ";
      printOperand(MI, OpNum, STI, O);
    }
    O << ")";
  }
}

void VEInstPrinter::printMemASOperandASX(const MCInst *MI, int OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O, const char *Modifier) {
  // If this is an ADD operand, emit it like normal operands.
  if (Modifier && !strcmp(Modifier, "arith")) {
    printOperand(MI, OpNum, STI, O);
    O << ", ";
    printOperand(MI, OpNum + 1, STI, O);
    return;
  }

  if (MI->getOperand(OpNum + 1).isImm() &&
      MI->getOperand(OpNum + 1).getImm() == 0) {
    // don't print "+0"
  } else {
    printOperand(MI, OpNum + 1, STI, O);
  }
  if (MI->getOperand(OpNum).isImm() && MI->getOperand(OpNum).getImm() == 0) {
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0) {
      O << "0";
    } else {
      // don't print "(0)"
    }
  } else {
    O << "(, ";
    printOperand(MI, OpNum, STI, O);
    O << ")";
  }
}

void VEInstPrinter::printMemASOperandRRM(const MCInst *MI, int OpNum,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O, const char *Modifier) {
  // If this is an ADD operand, emit it like normal operands.
  if (Modifier && !strcmp(Modifier, "arith")) {
    printOperand(MI, OpNum, STI, O);
    O << ", ";
    printOperand(MI, OpNum + 1, STI, O);
    return;
  }

  if (MI->getOperand(OpNum + 1).isImm() &&
      MI->getOperand(OpNum + 1).getImm() == 0) {
    // don't print "+0"
  } else {
    printOperand(MI, OpNum + 1, STI, O);
  }
  if (MI->getOperand(OpNum).isImm() && MI->getOperand(OpNum).getImm() == 0) {
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0) {
      O << "0";
    } else {
      // don't print "(0)"
    }
  } else {
    O << "(";
    printOperand(MI, OpNum, STI, O);
    O << ")";
  }
}

void VEInstPrinter::printMemASOperandHM(const MCInst *MI, int OpNum,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O, const char *Modifier) {
  // If this is an ADD operand, emit it like normal operands.
  if (Modifier && !strcmp(Modifier, "arith")) {
    printOperand(MI, OpNum, STI, O);
    O << ", ";
    printOperand(MI, OpNum + 1, STI, O);
    return;
  }

  if (MI->getOperand(OpNum + 1).isImm() &&
      MI->getOperand(OpNum + 1).getImm() == 0) {
    // don't print "+0"
  } else {
    printOperand(MI, OpNum + 1, STI, O);
  }
  O << "(";
  if (MI->getOperand(OpNum).isReg())
    printOperand(MI, OpNum, STI, O);
  O << ")";
}

void VEInstPrinter::printMImmOperand(const MCInst *MI, int OpNum,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  int MImm = (int)MI->getOperand(OpNum).getImm() & 0x7f;
  if (MImm > 63)
    O << "(" << MImm - 64 << ")0";
  else
    O << "(" << MImm << ")1";
}

void VEInstPrinter::printCCOperand(const MCInst *MI, int OpNum,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  int CC = (int)MI->getOperand(OpNum).getImm();
  O << VECondCodeToString((VECC::CondCode)CC);
}

void VEInstPrinter::printRDOperand(const MCInst *MI, int OpNum,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  int RD = (int)MI->getOperand(OpNum).getImm();
  O << VERDToString((VERD::RoundingMode)RD);
}
