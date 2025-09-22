//===-- llvm/CodeGen/AsmPrinterHandler.h -----------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a generic interface for AsmPrinter handlers,
// like debug and EH info emitters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTERHANDLER_H
#define LLVM_CODEGEN_ASMPRINTERHANDLER_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

class AsmPrinter;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MCSymbol;
class Module;

typedef MCSymbol *ExceptionSymbolProvider(AsmPrinter *Asm,
                                          const MachineBasicBlock *MBB);

/// Collects and handles AsmPrinter objects required to build debug
/// or EH information.
class AsmPrinterHandler {
public:
  virtual ~AsmPrinterHandler();

  virtual void beginModule(Module *M) {}

  /// Emit all sections that should come after the content.
  virtual void endModule() = 0;

  /// Gather pre-function debug information.
  /// Every beginFunction(MF) call should be followed by an endFunction(MF)
  /// call.
  virtual void beginFunction(const MachineFunction *MF) = 0;

  // Emit any of function marker (like .cfi_endproc). This is called
  // before endFunction and cannot switch sections.
  virtual void markFunctionEnd();

  /// Gather post-function debug information.
  virtual void endFunction(const MachineFunction *MF) = 0;

  /// Process the beginning of a new basic-block-section within a
  /// function. Always called immediately after beginFunction for the first
  /// basic-block. When basic-block-sections are enabled, called before the
  /// first block of each such section.
  virtual void beginBasicBlockSection(const MachineBasicBlock &MBB) {}

  /// Process the end of a basic-block-section within a function. When
  /// basic-block-sections are enabled, called after the last block in each such
  /// section (including the last section in the function). When
  /// basic-block-sections are disabled, called at the end of a function,
  /// immediately prior to markFunctionEnd.
  virtual void endBasicBlockSection(const MachineBasicBlock &MBB) {}

  /// Emit target-specific EH funclet machinery.
  virtual void beginFunclet(const MachineBasicBlock &MBB,
                            MCSymbol *Sym = nullptr) {}
  virtual void endFunclet() {}
};

} // End of namespace llvm

#endif
