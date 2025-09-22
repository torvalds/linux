//===- MipsMCInstLower.h - Lower MachineInstr to MCInst --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSMCINSTLOWER_H
#define LLVM_LIB_TARGET_MIPS_MIPSMCINSTLOWER_H

#include "MCTargetDesc/MipsMCExpr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;
class MCContext;
class MCInst;
class MCOperand;
class MipsAsmPrinter;

/// MipsMCInstLower - This class is used to lower an MachineInstr into an
///                   MCInst.
class LLVM_LIBRARY_VISIBILITY MipsMCInstLower {
  using MachineOperandType = MachineOperand::MachineOperandType;

  MCContext *Ctx;
  MipsAsmPrinter &AsmPrinter;

public:
  MipsMCInstLower(MipsAsmPrinter &asmprinter);

  void Initialize(MCContext *C);
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
  MCOperand LowerOperand(const MachineOperand &MO, int64_t offset = 0) const;

private:
  MCOperand LowerSymbolOperand(const MachineOperand &MO,
                               MachineOperandType MOTy, int64_t Offset) const;
  MCOperand createSub(MachineBasicBlock *BB1, MachineBasicBlock *BB2,
                      MipsMCExpr::MipsExprKind Kind) const;
  void lowerLongBranchLUi(const MachineInstr *MI, MCInst &OutMI) const;
  void lowerLongBranchADDiu(const MachineInstr *MI, MCInst &OutMI,
                            int Opcode) const;
  bool lowerLongBranch(const MachineInstr *MI, MCInst &OutMI) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSMCINSTLOWER_H
