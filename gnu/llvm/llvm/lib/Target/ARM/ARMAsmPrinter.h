//===-- ARMAsmPrinter.h - ARM implementation of AsmPrinter ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMASMPRINTER_H
#define LLVM_LIB_TARGET_ARM_ARMASMPRINTER_H

#include "ARMSubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class ARMFunctionInfo;
class MCOperand;
class MachineConstantPool;
class MachineOperand;
class MCSymbol;

namespace ARM {
  enum DW_ISA {
    DW_ISA_ARM_thumb = 1,
    DW_ISA_ARM_arm = 2
  };
}

class LLVM_LIBRARY_VISIBILITY ARMAsmPrinter : public AsmPrinter {

  /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
  /// make the right decision when printing asm code for different targets.
  const ARMSubtarget *Subtarget;

  /// AFI - Keep a pointer to ARMFunctionInfo for the current
  /// MachineFunction.
  ARMFunctionInfo *AFI;

  /// MCP - Keep a pointer to constantpool entries of the current
  /// MachineFunction.
  const MachineConstantPool *MCP;

  /// InConstantPool - Maintain state when emitting a sequence of constant
  /// pool entries so we can properly mark them as data regions.
  bool InConstantPool;

  /// ThumbIndirectPads - These maintain a per-function list of jump pad
  /// labels used for ARMv4t thumb code to make register indirect calls.
  SmallVector<std::pair<unsigned, MCSymbol*>, 4> ThumbIndirectPads;

  /// OptimizationGoals - Maintain a combined optimization goal for all
  /// functions in a module: one of Tag_ABI_optimization_goals values,
  /// -1 if uninitialized, 0 if conflicting goals
  int OptimizationGoals;

  /// List of globals that have had their storage promoted to a constant
  /// pool. This lives between calls to runOnMachineFunction and collects
  /// data from every MachineFunction. It is used during doFinalization
  /// when all non-function globals are emitted.
  SmallPtrSet<const GlobalVariable*,2> PromotedGlobals;
  /// Set of globals in PromotedGlobals that we've emitted labels for.
  /// We need to emit labels even for promoted globals so that DWARF
  /// debug info can link properly.
  SmallPtrSet<const GlobalVariable*,2> EmittedPromotedGlobalLabels;

public:
  explicit ARMAsmPrinter(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override {
    return "ARM Assembly Printer";
  }

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);

  void PrintSymbolOperand(const MachineOperand &MO, raw_ostream &O) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                       const char *ExtraCode, raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             const char *ExtraCode, raw_ostream &O) override;

  void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                        const MCSubtargetInfo *EndInfo) const override;

  void emitJumpTableAddrs(const MachineInstr *MI);
  void emitJumpTableInsts(const MachineInstr *MI);
  void emitJumpTableTBInst(const MachineInstr *MI, unsigned OffsetWidth);
  void emitInstruction(const MachineInstr *MI) override;
  bool runOnMachineFunction(MachineFunction &F) override;
  std::tuple<const MCSymbol *, uint64_t, const MCSymbol *,
             codeview::JumpTableEntrySize>
  getCodeViewJumpTableInfo(int JTI, const MachineInstr *BranchInstr,
                           const MCSymbol *BranchLabel) const override;

  void emitConstantPool() override {
    // we emit constant pools customly!
  }
  void emitFunctionBodyEnd() override;
  void emitFunctionEntryLabel() override;
  void emitStartOfAsmFile(Module &M) override;
  void emitEndOfAsmFile(Module &M) override;
  void emitXXStructor(const DataLayout &DL, const Constant *CV) override;
  void emitGlobalVariable(const GlobalVariable *GV) override;

  MCSymbol *GetCPISymbol(unsigned CPID) const override;

  // lowerOperand - Convert a MachineOperand into the equivalent MCOperand.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp);

  //===------------------------------------------------------------------===//
  // XRay implementation
  //===------------------------------------------------------------------===//
public:
  // XRay-specific lowering for ARM.
  void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI);
  void LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI);
  void LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI);

private:
  void EmitSled(const MachineInstr &MI, SledKind Kind);

  // Helpers for emitStartOfAsmFile() and emitEndOfAsmFile()
  void emitAttributes();

  void EmitUnwindingInstruction(const MachineInstr *MI);

  // emitPseudoExpansionLowering - tblgen'erated.
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

public:
  unsigned getISAEncoding() override {
    // ARM/Darwin adds ISA to the DWARF info for each function.
    const Triple &TT = TM.getTargetTriple();
    if (!TT.isOSBinFormatMachO())
      return 0;
    bool isThumb = TT.isThumb() ||
                   TT.getSubArch() == Triple::ARMSubArch_v7m ||
                   TT.getSubArch() == Triple::ARMSubArch_v6m;
    return isThumb ? ARM::DW_ISA_ARM_thumb : ARM::DW_ISA_ARM_arm;
  }

private:
  MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol);
  MCSymbol *GetARMJTIPICJumpTableLabel(unsigned uid) const;

  MCSymbol *GetARMGVSymbol(const GlobalValue *GV, unsigned char TargetFlags);

public:
  /// EmitMachineConstantPoolValue - Print a machine constantpool value to
  /// the .s file.
  void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;
};
} // end namespace llvm

#endif
