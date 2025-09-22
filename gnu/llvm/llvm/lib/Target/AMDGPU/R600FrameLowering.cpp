//===----------------------- R600FrameLowering.cpp ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//==-----------------------------------------------------------------------===//

#include "R600FrameLowering.h"
#include "R600Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

using namespace llvm;

R600FrameLowering::~R600FrameLowering() = default;

/// \returns The number of registers allocated for \p FI.
StackOffset
R600FrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const R600RegisterInfo *RI
    = MF.getSubtarget<R600Subtarget>().getRegisterInfo();

  // Fill in FrameReg output argument.
  FrameReg = RI->getFrameRegister(MF);

  // Start the offset at 2 so we don't overwrite work group information.
  // FIXME: We should only do this when the shader actually uses this
  // information.
  unsigned OffsetBytes = 2 * (getStackWidth(MF) * 4);
  int UpperBound = FI == -1 ? MFI.getNumObjects() : FI;

  for (int i = MFI.getObjectIndexBegin(); i < UpperBound; ++i) {
    OffsetBytes = alignTo(OffsetBytes, MFI.getObjectAlign(i));
    OffsetBytes += MFI.getObjectSize(i);
    // Each register holds 4 bytes, so we must always align the offset to at
    // least 4 bytes, so that 2 frame objects won't share the same register.
    OffsetBytes = alignTo(OffsetBytes, Align(4));
  }

  if (FI != -1)
    OffsetBytes = alignTo(OffsetBytes, MFI.getObjectAlign(FI));

  return StackOffset::getFixed(OffsetBytes / (getStackWidth(MF) * 4));
}
