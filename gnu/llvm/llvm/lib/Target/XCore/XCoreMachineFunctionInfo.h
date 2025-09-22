//===- XCoreMachineFunctionInfo.h - XCore machine function info -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares XCore-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCOREMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_XCORE_XCOREMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

/// XCoreFunctionInfo - This class is derived from MachineFunction private
/// XCore target-specific information for each MachineFunction.
class XCoreFunctionInfo : public MachineFunctionInfo {
  bool LRSpillSlotSet = false;
  int LRSpillSlot;
  bool FPSpillSlotSet = false;
  int FPSpillSlot;
  bool EHSpillSlotSet = false;
  int EHSpillSlot[2];
  unsigned ReturnStackOffset;
  bool ReturnStackOffsetSet = false;
  int VarArgsFrameIndex = 0;
  mutable int CachedEStackSize = -1;
  std::vector<std::pair<MachineBasicBlock::iterator, CalleeSavedInfo>>
  SpillLabels;

  virtual void anchor();

public:
  XCoreFunctionInfo() = default;

  explicit XCoreFunctionInfo(const Function &F,
                             const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  ~XCoreFunctionInfo() override = default;

  void setVarArgsFrameIndex(int off) { VarArgsFrameIndex = off; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }

  int createLRSpillSlot(MachineFunction &MF);
  bool hasLRSpillSlot() { return LRSpillSlotSet; }
  int getLRSpillSlot() const {
    assert(LRSpillSlotSet && "LR Spill slot not set");
    return LRSpillSlot;
  }

  int createFPSpillSlot(MachineFunction &MF);
  bool hasFPSpillSlot() { return FPSpillSlotSet; }
  int getFPSpillSlot() const {
    assert(FPSpillSlotSet && "FP Spill slot not set");
    return FPSpillSlot;
  }

  const int* createEHSpillSlot(MachineFunction &MF);
  bool hasEHSpillSlot() { return EHSpillSlotSet; }
  const int* getEHSpillSlot() const {
    assert(EHSpillSlotSet && "EH Spill slot not set");
    return EHSpillSlot;
  }

  void setReturnStackOffset(unsigned value) {
    assert(!ReturnStackOffsetSet && "Return stack offset set twice");
    ReturnStackOffset = value;
    ReturnStackOffsetSet = true;
  }

  unsigned getReturnStackOffset() const {
    assert(ReturnStackOffsetSet && "Return stack offset not set");
    return ReturnStackOffset;
  }

  bool isLargeFrame(const MachineFunction &MF) const;

  std::vector<std::pair<MachineBasicBlock::iterator, CalleeSavedInfo>> &
  getSpillLabels() {
    return SpillLabels;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_XCORE_XCOREMACHINEFUNCTIONINFO_H
