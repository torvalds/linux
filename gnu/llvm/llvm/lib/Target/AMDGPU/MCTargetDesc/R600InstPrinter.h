//===-- R600InstPrinter.h - AMDGPU MC Inst -> ASM interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_R600INSTPRINTER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_R600INSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class R600InstPrinter : public MCInstPrinter {
public:
  R600InstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                  const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;
  std::pair<const char *, uint64_t> getMnemonic(const MCInst *MI) override;
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  static const char *getRegisterName(MCRegister Reg);

  void printAbs(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printBankSwizzle(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printClamp(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printCT(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printKCache(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printLast(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printLiteral(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printNeg(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printOMOD(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printRel(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printRSel(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printUpdateExecMask(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printUpdatePred(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printWrite(const MCInst *MI, unsigned OpNo, raw_ostream &O);
};

} // End namespace llvm

#endif
