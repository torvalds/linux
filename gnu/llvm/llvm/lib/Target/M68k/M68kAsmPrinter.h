//===-- M68kAsmPrinter.h - M68k LLVM Assembly Printer -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains M68k assembler printer declarations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KASMPRINTER_H
#define LLVM_LIB_TARGET_M68K_M68KASMPRINTER_H

#include "M68kMCInstLower.h"
#include "M68kTargetMachine.h"
#include "MCTargetDesc/M68kMemOperandPrinter.h"

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"
#include <memory>
#include <utility>

namespace llvm {
class MCStreamer;
class MachineInstr;
class MachineBasicBlock;
class Module;
class raw_ostream;

class M68kSubtarget;
class M68kMachineFunctionInfo;

class LLVM_LIBRARY_VISIBILITY M68kAsmPrinter
    : public AsmPrinter,
      public M68kMemOperandPrinter<M68kAsmPrinter, MachineInstr> {

  friend class M68kMemOperandPrinter;

  void EmitInstrWithMacroNoAT(const MachineInstr *MI);

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &OS);

  void printDisp(const MachineInstr *MI, unsigned OpNum, raw_ostream &OS);
  void printAbsMem(const MachineInstr *MI, unsigned OpNum, raw_ostream &OS);

public:
  const M68kSubtarget *Subtarget;
  const M68kMachineFunctionInfo *MMFI;
  std::unique_ptr<M68kMCInstLower> MCInstLowering;

  explicit M68kAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {
    Subtarget = static_cast<M68kTargetMachine &>(TM).getSubtargetImpl();
  }

  StringRef getPassName() const override { return "M68k Assembly Printer"; }

  virtual bool runOnMachineFunction(MachineFunction &MF) override;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionBodyStart() override;
  void emitFunctionBodyEnd() override;
  void emitStartOfAsmFile(Module &M) override;
  void emitEndOfAsmFile(Module &M) override;
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KASMPRINTER_H
