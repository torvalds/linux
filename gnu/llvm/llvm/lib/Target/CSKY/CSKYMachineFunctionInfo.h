//=- CSKYMachineFunctionInfo.h - CSKY machine function info -------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares CSKY-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_CSKY_CSKYMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class CSKYMachineFunctionInfo : public MachineFunctionInfo {
  Register GlobalBaseReg = 0;
  bool SpillsCR = false;

  int VarArgsFrameIndex = 0;
  unsigned VarArgsSaveSize = 0;

  int spillAreaSize = 0;

  bool LRSpilled = false;

  unsigned PICLabelUId = 0;

public:
  CSKYMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<CSKYMachineFunctionInfo>(*this);
  }

  Register getGlobalBaseReg() const { return GlobalBaseReg; }
  void setGlobalBaseReg(Register Reg) { GlobalBaseReg = Reg; }

  void setSpillsCR() { SpillsCR = true; }
  bool isCRSpilled() const { return SpillsCR; }

  void setVarArgsFrameIndex(int v) { VarArgsFrameIndex = v; }
  int getVarArgsFrameIndex() { return VarArgsFrameIndex; }

  unsigned getVarArgsSaveSize() const { return VarArgsSaveSize; }
  void setVarArgsSaveSize(int Size) { VarArgsSaveSize = Size; }

  bool isLRSpilled() const { return LRSpilled; }
  void setLRIsSpilled(bool s) { LRSpilled = s; }

  void setCalleeSaveAreaSize(int v) { spillAreaSize = v; }
  int getCalleeSaveAreaSize() const { return spillAreaSize; }

  unsigned createPICLabelUId() { return ++PICLabelUId; }
  void initPICLabelUId(unsigned UId) { PICLabelUId = UId; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYMACHINEFUNCTIONINFO_H
