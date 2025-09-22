//===-- CSKYAsmPrinter.h - CSKY implementation of AsmPrinter ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYASMPRINTER_H
#define LLVM_LIB_TARGET_CSKY_CSKYASMPRINTER_H

#include "CSKYMCInstLower.h"
#include "CSKYSubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCDirectives.h"

namespace llvm {
class LLVM_LIBRARY_VISIBILITY CSKYAsmPrinter : public AsmPrinter {
  CSKYMCInstLower MCInstLowering;

  const MCSubtargetInfo *Subtarget;
  const TargetInstrInfo *TII;

  bool InConstantPool = false;

  /// Keep a pointer to constantpool entries of the current
  /// MachineFunction.
  MachineConstantPool *MCP;

  void expandTLSLA(const MachineInstr *MI);
  void emitCustomConstantPool(const MachineInstr *MI);
  void emitAttributes();

public:
  explicit CSKYAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override { return "CSKY Assembly Printer"; }

  void EmitToStreamer(MCStreamer &S, const MCInst &Inst);

  /// tblgen'erated driver function for lowering simple MI->MC
  /// pseudo instructions.
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

  void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;

  void emitFunctionBodyEnd() override;

  void emitStartOfAsmFile(Module &M) override;

  void emitEndOfAsmFile(Module &M) override;

  void emitInstruction(const MachineInstr *MI) override;

  bool runOnMachineFunction(MachineFunction &MF) override;

  // we emit constant pools customly!
  void emitConstantPool() override {}

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;

  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYASMPRINTER_H
