//===-- ARMWinEH.cpp - Windows on ARM EH Support Functions ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ARMWinEH.h"

namespace llvm {
namespace ARM {
namespace WinEH {
std::pair<uint16_t, uint32_t> SavedRegisterMask(const RuntimeFunction &RF,
                                                bool Prologue) {
  uint8_t NumRegisters = RF.Reg();
  uint8_t RegistersVFP = RF.R();
  uint8_t LinkRegister = RF.L();
  uint8_t ChainedFrame = RF.C();

  uint16_t GPRMask = (ChainedFrame << 11);
  uint32_t VFPMask = 0;

  if (Prologue) {
    GPRMask |= (LinkRegister << 14);
  } else {
    // If Ret != 0, we pop into Lr and return later
    if (RF.Ret() != ReturnType::RT_POP)
      GPRMask |= (LinkRegister << 14);
    else if (!RF.H()) // If H == 0, we pop directly into Pc
      GPRMask |= (LinkRegister << 15);
    // else, Ret == 0 && H == 1, we pop into Pc separately afterwards
  }

  if (RegistersVFP)
    VFPMask |= (((1 << ((NumRegisters + 1) % 8)) - 1) << 8);
  else
    GPRMask |= (((1 << (NumRegisters + 1)) - 1) << 4);

  if ((PrologueFolding(RF) && Prologue) || (EpilogueFolding(RF) && !Prologue))
    GPRMask |= (((1 << ((RF.StackAdjust() & 0x3) + 1)) - 1)
                << (~RF.StackAdjust() & 0x3));

  return std::make_pair(GPRMask, VFPMask);
}
} // namespace WinEH
} // namespace ARM
} // namespace llvm

