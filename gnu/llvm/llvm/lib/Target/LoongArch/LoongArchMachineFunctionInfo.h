//=- LoongArchMachineFunctionInfo.h - LoongArch machine function info -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares LoongArch-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_LOONGARCHMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_LOONGARCH_LOONGARCHMACHINEFUNCTIONINFO_H

#include "LoongArchSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// LoongArchMachineFunctionInfo - This class is derived from
/// MachineFunctionInfo and contains private LoongArch-specific information for
/// each MachineFunction.
class LoongArchMachineFunctionInfo : public MachineFunctionInfo {
private:
  /// FrameIndex for start of varargs area
  int VarArgsFrameIndex = 0;
  /// Size of the save area used for varargs
  int VarArgsSaveSize = 0;

  /// Size of stack frame to save callee saved registers
  unsigned CalleeSavedStackSize = 0;

  /// FrameIndex of the spill slot when there is no scavenged register in
  /// insertIndirectBranch.
  int BranchRelaxationSpillFrameIndex = -1;

  /// Registers that have been sign extended from i32.
  SmallVector<Register, 8> SExt32Registers;

public:
  LoongArchMachineFunctionInfo(const Function &F,
                               const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<LoongArchMachineFunctionInfo>(*this);
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  unsigned getVarArgsSaveSize() const { return VarArgsSaveSize; }
  void setVarArgsSaveSize(int Size) { VarArgsSaveSize = Size; }

  unsigned getCalleeSavedStackSize() const { return CalleeSavedStackSize; }
  void setCalleeSavedStackSize(unsigned Size) { CalleeSavedStackSize = Size; }

  int getBranchRelaxationSpillFrameIndex() {
    return BranchRelaxationSpillFrameIndex;
  }
  void setBranchRelaxationSpillFrameIndex(int Index) {
    BranchRelaxationSpillFrameIndex = Index;
  }

  void addSExt32Register(Register Reg) { SExt32Registers.push_back(Reg); }

  bool isSExt32Register(Register Reg) const {
    return is_contained(SExt32Registers, Reg);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LOONGARCH_LOONGARCHMACHINEFUNCTIONINFO_H
