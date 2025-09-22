//===-- R600Subtarget.cpp - R600 Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements the R600 specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "R600Subtarget.h"
#include "MCTargetDesc/R600MCTargetDesc.h"

using namespace llvm;

#define DEBUG_TYPE "r600-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "R600GenSubtargetInfo.inc"

R600Subtarget::R600Subtarget(const Triple &TT, StringRef GPU, StringRef FS,
                             const TargetMachine &TM)
    : R600GenSubtargetInfo(TT, GPU, /*TuneCPU*/ GPU, FS), AMDGPUSubtarget(TT),
      InstrInfo(*this),
      FrameLowering(TargetFrameLowering::StackGrowsUp, getStackAlignment(), 0),
      TLInfo(TM, initializeSubtargetDependencies(TT, GPU, FS)),
      InstrItins(getInstrItineraryForCPU(GPU)) {
  AddressableLocalMemorySize = LocalMemorySize;
}

R600Subtarget &R600Subtarget::initializeSubtargetDependencies(const Triple &TT,
                                                              StringRef GPU,
                                                              StringRef FS) {
  SmallString<256> FullFS("+promote-alloca,");
  FullFS += FS;
  ParseSubtargetFeatures(GPU, /*TuneCPU*/ GPU, FullFS);

  HasMulU24 = getGeneration() >= EVERGREEN;
  HasMulI24 = hasCaymanISA();

  return *this;
}
