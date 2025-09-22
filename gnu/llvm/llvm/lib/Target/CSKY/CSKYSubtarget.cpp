//===-- CSKYSubtarget.h - Define Subtarget for the CSKY----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CSKY specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "CSKYSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

using namespace llvm;

#define DEBUG_TYPE "csky-subtarget"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "CSKYGenSubtargetInfo.inc"

void CSKYSubtarget::anchor() {}

CSKYSubtarget &CSKYSubtarget::initializeSubtargetDependencies(
    const Triple &TT, StringRef CPUName, StringRef TuneCPUName, StringRef FS) {

  if (CPUName.empty())
    CPUName = "generic";
  if (TuneCPUName.empty())
    TuneCPUName = CPUName;

  UseHardFloat = false;
  UseHardFloatABI = false;
  HasFPUv2SingleFloat = false;
  HasFPUv2DoubleFloat = false;
  HasFPUv3HalfWord = false;
  HasFPUv3HalfFloat = false;
  HasFPUv3SingleFloat = false;
  HasFPUv3DoubleFloat = false;
  HasFdivdu = false;
  HasFLOATE1 = false;
  HasFLOAT1E2 = false;
  HasFLOAT1E3 = false;
  HasFLOAT3E4 = false;
  HasFLOAT7E60 = false;
  HasExtendLrw = false;
  HasBTST16 = false;
  HasTrust = false;
  HasJAVA = false;
  HasCache = false;
  HasNVIC = false;
  HasDSP = false;
  HasDSP1E2 = false;
  HasDSPE60 = false;
  HasDSPV2 = false;
  HasDSP_Silan = false;
  HasDoloop = false;
  HasHardwareDivide = false;
  HasHighRegisters = false;
  HasVDSPV2 = false;
  HasVDSP2E3 = false;
  HasVDSP2E60F = false;
  ReadTPHard = false;
  HasVDSPV1_128 = false;
  UseCCRT = false;
  DumpConstPool = false;
  EnableInterruptAttribute = false;
  HasPushPop = false;
  HasSTM = false;
  SmartMode = false;
  EnableStackSize = false;

  HasE1 = false;
  HasE2 = false;
  Has2E3 = false;
  HasMP = false;
  Has3E3r1 = false;
  Has3r1E3r2 = false;
  Has3r2E3r3 = false;
  Has3E7 = false;
  HasMP1E2 = false;
  Has7E10 = false;
  Has10E60 = false;

  ParseSubtargetFeatures(CPUName, TuneCPUName, FS);
  return *this;
}

CSKYSubtarget::CSKYSubtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                             StringRef FS, const TargetMachine &TM)
    : CSKYGenSubtargetInfo(TT, CPU, TuneCPU, FS),
      FrameLowering(initializeSubtargetDependencies(TT, CPU, TuneCPU, FS)),
      InstrInfo(*this), RegInfo(), TLInfo(TM, *this) {}

bool CSKYSubtarget::useHardFloatABI() const {
  auto FloatABI = getTargetLowering()->getTargetMachine().Options.FloatABIType;

  if (FloatABI == FloatABI::Default)
    return UseHardFloatABI;
  else
    return FloatABI == FloatABI::Hard;
}
