//===-- HexagonInstPrinter.h - Convert Hexagon MCInst to assembly syntax --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_INSTPRINTER_HEXAGONINSTPRINTER_H
#define LLVM_LIB_TARGET_HEXAGON_INSTPRINTER_HEXAGONINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
/// Prints bundles as a newline separated list of individual instructions
/// Duplexes are separated by a vertical tab \v character
/// A trailing line includes bundle properties such as endloop0/1
///
/// r0 = add(r1, r2)
/// r0 = #0 \v jump 0x0
/// :endloop0 :endloop1
class HexagonInstPrinter : public MCInstPrinter {
public:
  explicit HexagonInstPrinter(MCAsmInfo const &MAI, MCInstrInfo const &MII,
                              MCRegisterInfo const &MRI)
    : MCInstPrinter(MAI, MII, MRI), MII(MII) {}

  void printInst(MCInst const *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override;
  void printRegName(raw_ostream &O, MCRegister Reg) const override;

  static char const *getRegisterName(MCRegister Reg);

  std::pair<const char *, uint64_t> getMnemonic(const MCInst *MI) override;
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  void printOperand(MCInst const *MI, unsigned OpNo, raw_ostream &O) const;
  void printBrtarget(MCInst const *MI, unsigned OpNo, raw_ostream &O) const;

  MCAsmInfo const &getMAI() const { return MAI; }
  MCInstrInfo const &getMII() const { return MII; }

private:
  MCInstrInfo const &MII;
  bool HasExtender = false;
};

} // end namespace llvm

#endif
