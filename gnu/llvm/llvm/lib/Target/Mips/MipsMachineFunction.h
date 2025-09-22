//===- MipsMachineFunctionInfo.h - Private data used for Mips ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Mips specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSMACHINEFUNCTION_H
#define LLVM_LIB_TARGET_MIPS_MIPSMACHINEFUNCTION_H

#include "Mips16HardFloatInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include <map>

namespace llvm {

/// MipsFunctionInfo - This class is derived from MachineFunction private
/// Mips target-specific information for each MachineFunction.
class MipsFunctionInfo : public MachineFunctionInfo {
public:
  MipsFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  ~MipsFunctionInfo() override;

  unsigned getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(unsigned Reg) { SRetReturnReg = Reg; }

  bool globalBaseRegSet() const;
  Register getGlobalBaseReg(MachineFunction &MF);
  Register getGlobalBaseRegForGlobalISel(MachineFunction &MF);

  // Insert instructions to initialize the global base register in the
  // first MBB of the function.
  void initGlobalBaseReg(MachineFunction &MF);

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  bool hasByvalArg() const { return HasByvalArg; }
  void setFormalArgInfo(unsigned Size, bool HasByval) {
    IncomingArgSize = Size;
    HasByvalArg = HasByval;
  }

  unsigned getIncomingArgSize() const { return IncomingArgSize; }

  bool callsEhReturn() const { return CallsEhReturn; }
  void setCallsEhReturn() { CallsEhReturn = true; }

  void createEhDataRegsFI(MachineFunction &MF);
  int getEhDataRegFI(unsigned Reg) const { return EhDataRegFI[Reg]; }
  bool isEhDataRegFI(int FI) const;

  /// Create a MachinePointerInfo that has an ExternalSymbolPseudoSourceValue
  /// object representing a GOT entry for an external function.
  MachinePointerInfo callPtrInfo(MachineFunction &MF, const char *ES);

  // Functions with the "interrupt" attribute require special prologues,
  // epilogues and additional spill slots.
  bool isISR() const { return IsISR; }
  void setISR() { IsISR = true; }
  void createISRRegFI(MachineFunction &MF);
  int getISRRegFI(Register Reg) const { return ISRDataRegFI[Reg]; }
  bool isISRRegFI(int FI) const;

  /// Create a MachinePointerInfo that has a GlobalValuePseudoSourceValue object
  /// representing a GOT entry for a global function.
  MachinePointerInfo callPtrInfo(MachineFunction &MF, const GlobalValue *GV);

  void setSaveS2() { SaveS2 = true; }
  bool hasSaveS2() const { return SaveS2; }

  int getMoveF64ViaSpillFI(MachineFunction &MF, const TargetRegisterClass *RC);

  std::map<const char *, const Mips16HardFloatInfo::FuncSignature *>
  StubsNeeded;

private:
  virtual void anchor();

  /// SRetReturnReg - Some subtargets require that sret lowering includes
  /// returning the value of the returned struct in a register. This field
  /// holds the virtual register into which the sret argument is passed.
  Register SRetReturnReg;

  /// GlobalBaseReg - keeps track of the virtual register initialized for
  /// use as the global base register. This is used for PIC in some PIC
  /// relocation models.
  Register GlobalBaseReg;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;

  /// True if function has a byval argument.
  bool HasByvalArg;

  /// Size of incoming argument area.
  unsigned IncomingArgSize;

  /// CallsEhReturn - Whether the function calls llvm.eh.return.
  bool CallsEhReturn = false;

  /// Frame objects for spilling eh data registers.
  int EhDataRegFI[4];

  /// ISR - Whether the function is an Interrupt Service Routine.
  bool IsISR = false;

  /// Frame objects for spilling C0_STATUS, C0_EPC
  int ISRDataRegFI[2];

  // saveS2
  bool SaveS2 = false;

  /// FrameIndex for expanding BuildPairF64 nodes to spill and reload when the
  /// O32 FPXX ABI is enabled. -1 is used to denote invalid index.
  int MoveF64ViaSpillFI = -1;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSMACHINEFUNCTION_H
