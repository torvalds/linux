//===-- R600InstPrinter.cpp - AMDGPU MC Inst -> ASM ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// \file
//===----------------------------------------------------------------------===//

#include "R600InstPrinter.h"
#include "AMDGPUInstPrinter.h"
#include "R600MCTargetDesc.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

void R600InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void R600InstPrinter::printAbs(const MCInst *MI, unsigned OpNo,
                               raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, '|');
}

void R600InstPrinter::printBankSwizzle(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  int BankSwizzle = MI->getOperand(OpNo).getImm();
  switch (BankSwizzle) {
  case 1:
    O << "BS:VEC_021/SCL_122";
    break;
  case 2:
    O << "BS:VEC_120/SCL_212";
    break;
  case 3:
    O << "BS:VEC_102/SCL_221";
    break;
  case 4:
    O << "BS:VEC_201";
    break;
  case 5:
    O << "BS:VEC_210";
    break;
  default:
    break;
  }
}

void R600InstPrinter::printClamp(const MCInst *MI, unsigned OpNo,
                                 raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, "_SAT");
}

void R600InstPrinter::printCT(const MCInst *MI, unsigned OpNo, raw_ostream &O) {
  unsigned CT = MI->getOperand(OpNo).getImm();
  switch (CT) {
  case 0:
    O << 'U';
    break;
  case 1:
    O << 'N';
    break;
  default:
    break;
  }
}

void R600InstPrinter::printKCache(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  int KCacheMode = MI->getOperand(OpNo).getImm();
  if (KCacheMode > 0) {
    int KCacheBank = MI->getOperand(OpNo - 2).getImm();
    O << "CB" << KCacheBank << ':';
    int KCacheAddr = MI->getOperand(OpNo + 2).getImm();
    int LineSize = (KCacheMode == 1) ? 16 : 32;
    O << KCacheAddr * 16 << '-' << KCacheAddr * 16 + LineSize;
  }
}

void R600InstPrinter::printLast(const MCInst *MI, unsigned OpNo,
                                raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, "*", " ");
}

void R600InstPrinter::printLiteral(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() || Op.isExpr());
  if (Op.isImm()) {
    int64_t Imm = Op.getImm();
    O << Imm << '(' << llvm::bit_cast<float>(static_cast<uint32_t>(Imm)) << ')';
  }
  if (Op.isExpr()) {
    Op.getExpr()->print(O << '@', &MAI);
  }
}

void R600InstPrinter::printNeg(const MCInst *MI, unsigned OpNo,
                               raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, '-');
}

void R600InstPrinter::printOMOD(const MCInst *MI, unsigned OpNo,
                                raw_ostream &O) {
  switch (MI->getOperand(OpNo).getImm()) {
  default:
    break;
  case 1:
    O << " * 2.0";
    break;
  case 2:
    O << " * 4.0";
    break;
  case 3:
    O << " / 2.0";
    break;
  }
}

void R600InstPrinter::printMemOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  printOperand(MI, OpNo, O);
  O << ", ";
  printOperand(MI, OpNo + 1, O);
}

void R600InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  if (OpNo >= MI->getNumOperands()) {
    O << "/*Missing OP" << OpNo << "*/";
    return;
  }

  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    switch (Op.getReg()) {
    // This is the default predicate state, so we don't need to print it.
    case R600::PRED_SEL_OFF:
      break;

    default:
      O << getRegisterName(Op.getReg());
      break;
    }
  } else if (Op.isImm()) {
    O << Op.getImm();
  } else if (Op.isDFPImm()) {
    // We special case 0.0 because otherwise it will be printed as an integer.
    if (Op.getDFPImm() == 0.0)
      O << "0.0";
    else {
      O << bit_cast<double>(Op.getDFPImm());
    }
  } else if (Op.isExpr()) {
    const MCExpr *Exp = Op.getExpr();
    Exp->print(O, &MAI);
  } else {
    O << "/*INV_OP*/";
  }
}

void R600InstPrinter::printRel(const MCInst *MI, unsigned OpNo,
                               raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, '+');
}

void R600InstPrinter::printRSel(const MCInst *MI, unsigned OpNo,
                                raw_ostream &O) {
  unsigned Sel = MI->getOperand(OpNo).getImm();
  switch (Sel) {
  case 0:
    O << 'X';
    break;
  case 1:
    O << 'Y';
    break;
  case 2:
    O << 'Z';
    break;
  case 3:
    O << 'W';
    break;
  case 4:
    O << '0';
    break;
  case 5:
    O << '1';
    break;
  case 7:
    O << '_';
    break;
  default:
    break;
  }
}

void R600InstPrinter::printUpdateExecMask(const MCInst *MI, unsigned OpNo,
                                          raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, "ExecMask,");
}

void R600InstPrinter::printUpdatePred(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  AMDGPUInstPrinter::printIfSet(MI, OpNo, O, "Pred,");
}

void R600InstPrinter::printWrite(const MCInst *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.getImm() == 0) {
    O << " (MASKED)";
  }
}

#include "R600GenAsmWriter.inc"
