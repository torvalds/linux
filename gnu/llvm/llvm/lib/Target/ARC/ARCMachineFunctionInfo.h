//===- ARCMachineFunctionInfo.h - ARC machine function info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares ARC-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_ARC_ARCMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include <vector>

namespace llvm {

/// ARCFunctionInfo - This class is derived from MachineFunction private
/// ARC target-specific information for each MachineFunction.
class ARCFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();
  bool ReturnStackOffsetSet;
  int VarArgsFrameIndex;
  unsigned ReturnStackOffset;

public:
  explicit ARCFunctionInfo(const Function &F, const TargetSubtargetInfo *STI)
      : ReturnStackOffsetSet(false), VarArgsFrameIndex(0),
        ReturnStackOffset(-1U), MaxCallStackReq(0) {}
  ~ARCFunctionInfo() {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  void setVarArgsFrameIndex(int off) { VarArgsFrameIndex = off; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }

  void setReturnStackOffset(unsigned value) {
    assert(!ReturnStackOffsetSet && "Return stack offset set twice");
    ReturnStackOffset = value;
    ReturnStackOffsetSet = true;
  }

  unsigned getReturnStackOffset() const {
    assert(ReturnStackOffsetSet && "Return stack offset not set");
    return ReturnStackOffset;
  }

  unsigned MaxCallStackReq;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCMACHINEFUNCTIONINFO_H
