//===-- VESubtarget.cpp - VE Subtarget Information ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the VE specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "VESubtarget.h"
#include "VE.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "ve-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "VEGenSubtargetInfo.inc"

void VESubtarget::anchor() {}

VESubtarget &VESubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                          StringRef FS) {
  // Default feature settings
  EnableVPU = false;

  // Determine default and user specified characteristics
  std::string CPUName = std::string(CPU);
  if (CPUName.empty())
    CPUName = "generic";

  // Parse features string.
  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPU, FS);

  return *this;
}

VESubtarget::VESubtarget(const Triple &TT, const std::string &CPU,
                         const std::string &FS, const TargetMachine &TM)
    : VEGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS), TargetTriple(TT),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {}

uint64_t VESubtarget::getAdjustedFrameSize(uint64_t FrameSize) const {
  // Calculate adjusted frame size by adding the size of RSA frame,
  // return address, and frame poitner as described in VEFrameLowering.cpp.
  const VEFrameLowering *TFL = getFrameLowering();

  FrameSize += getRsaSize();
  FrameSize = alignTo(FrameSize, TFL->getStackAlign());

  return FrameSize;
}

bool VESubtarget::enableMachineScheduler() const { return true; }
