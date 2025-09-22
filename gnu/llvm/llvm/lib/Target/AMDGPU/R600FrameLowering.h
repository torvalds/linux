//===--------------------- R600FrameLowering.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H

#include "AMDGPUFrameLowering.h"

namespace llvm {

class R600FrameLowering : public AMDGPUFrameLowering {
public:
  R600FrameLowering(StackDirection D, Align StackAl, int LAO,
                    Align TransAl = Align(1))
      : AMDGPUFrameLowering(D, StackAl, LAO, TransAl) {}
  ~R600FrameLowering() override;

  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override {}
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override {}
  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  bool hasFP(const MachineFunction &MF) const override {
    return false;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H
