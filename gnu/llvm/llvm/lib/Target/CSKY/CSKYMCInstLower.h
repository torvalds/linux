//===-- CSKYMCInstLower.cpp - Convert CSKY MachineInstr to an MCInst --------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYMCINSTLOWER_H
#define LLVM_LIB_TARGET_CSKY_CSKYMCINSTLOWER_H

namespace llvm {
class AsmPrinter;
class MCContext;
class MachineInstr;
class MCInst;
class MachineOperand;
class MCOperand;
class MCSymbol;

class CSKYMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  CSKYMCInstLower(MCContext &Ctx, AsmPrinter &Printer);

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const;
  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYMCINSTLOWER_H
