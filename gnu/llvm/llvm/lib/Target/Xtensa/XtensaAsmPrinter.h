//===- XtensaAsmPrinter.h - Xtensa LLVM Assembly Printer --------*- C++-*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Xtensa Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XTENSA_XTENSAASMPRINTER_H
#define LLVM_LIB_TARGET_XTENSA_XTENSAASMPRINTER_H

#include "XtensaTargetMachine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCStreamer;
class MachineBasicBlock;
class MachineInstr;
class Module;
class raw_ostream;

class LLVM_LIBRARY_VISIBILITY XtensaAsmPrinter : public AsmPrinter {
  const MCSubtargetInfo *STI;

public:
  explicit XtensaAsmPrinter(TargetMachine &TM,
                            std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), STI(TM.getMCSubtargetInfo()) {}

  StringRef getPassName() const override { return "Xtensa Assembly Printer"; }
  void emitInstruction(const MachineInstr *MI) override;

  void emitConstantPool() override;

  void emitMachineConstantPoolEntry(const MachineConstantPoolEntry &CPE, int i);

  void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;

  MCSymbol *GetConstantPoolIndexSymbol(const MachineOperand &MO) const;

  MCSymbol *GetJumpTableSymbol(const MachineOperand &MO) const;

  MCOperand LowerSymbolOperand(const MachineOperand &MO,
                               MachineOperand::MachineOperandType MOTy,
                               unsigned Offset) const;

  // Lower MachineInstr MI to MCInst OutMI.
  void lowerToMCInst(const MachineInstr *MI, MCInst &OutMI) const;

  // Return an MCOperand for MO.  Return an empty operand if MO is implicit.
  MCOperand lowerOperand(const MachineOperand &MO, unsigned Offset = 0) const;
};
} // end namespace llvm

#endif /* LLVM_LIB_TARGET_XTENSA_XTENSAASMPRINTER_H */
