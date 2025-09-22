//===-- LoongArchSubtarget.cpp - LoongArch Subtarget Information -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LoongArch specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "LoongArchSubtarget.h"
#include "LoongArchFrameLowering.h"
#include "MCTargetDesc/LoongArchBaseInfo.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "LoongArchGenSubtargetInfo.inc"

void LoongArchSubtarget::anchor() {}

LoongArchSubtarget &LoongArchSubtarget::initializeSubtargetDependencies(
    const Triple &TT, StringRef CPU, StringRef TuneCPU, StringRef FS,
    StringRef ABIName) {
  bool Is64Bit = TT.isArch64Bit();
  if (CPU.empty() || CPU == "generic")
    CPU = Is64Bit ? "generic-la64" : "generic-la32";

  if (TuneCPU.empty())
    TuneCPU = CPU;

  ParseSubtargetFeatures(CPU, TuneCPU, FS);
  initializeProperties(TuneCPU);
  if (Is64Bit) {
    GRLenVT = MVT::i64;
    GRLen = 64;
  }

  if (HasLA32 == HasLA64)
    report_fatal_error("Please use one feature of 32bit and 64bit.");

  if (Is64Bit && HasLA32)
    report_fatal_error("Feature 32bit should be used for loongarch32 target.");

  if (!Is64Bit && HasLA64)
    report_fatal_error("Feature 64bit should be used for loongarch64 target.");

  TargetABI = LoongArchABI::computeTargetABI(TT, getFeatureBits(), ABIName);

  return *this;
}

void LoongArchSubtarget::initializeProperties(StringRef TuneCPU) {
  // Initialize CPU specific properties. We should add a tablegen feature for
  // this in the future so we can specify it together with the subtarget
  // features.

  // TODO: Check TuneCPU and override defaults (that are for LA464) once we
  // support optimizing for more uarchs.

  // Default to the alignment settings empirically confirmed to perform best
  // on LA464, with 4-wide instruction fetch and decode stages. These settings
  // can also be overridden in initializeProperties.
  //
  // We default to such higher-than-minimum alignments because we assume that:
  //
  // * these settings should benefit most existing uarchs/users,
  // * future general-purpose LoongArch cores are likely to have issue widths
  //   equal to or wider than 4,
  // * instruction sequences best for LA464 should not pessimize other future
  //   uarchs, and
  // * narrower cores would not suffer much (aside from slightly increased
  //   ICache footprint maybe), compared to the gains everywhere else.
  PrefFunctionAlignment = Align(32);
  PrefLoopAlignment = Align(16);
  MaxBytesForAlignment = 16;
}

LoongArchSubtarget::LoongArchSubtarget(const Triple &TT, StringRef CPU,
                                       StringRef TuneCPU, StringRef FS,
                                       StringRef ABIName,
                                       const TargetMachine &TM)
    : LoongArchGenSubtargetInfo(TT, CPU, TuneCPU, FS),
      FrameLowering(
          initializeSubtargetDependencies(TT, CPU, TuneCPU, FS, ABIName)),
      InstrInfo(*this), RegInfo(getHwMode()), TLInfo(TM, *this) {}
