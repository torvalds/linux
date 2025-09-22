//===- ARCMCTargetDesc.cpp - ARC Target Descriptions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides ARC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "ARCMCTargetDesc.h"
#include "ARCInstPrinter.h"
#include "ARCMCAsmInfo.h"
#include "ARCTargetStreamer.h"
#include "TargetInfo/ARCTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "ARCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "ARCGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "ARCGenRegisterInfo.inc"

static MCInstrInfo *createARCMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitARCMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createARCMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  InitARCMCRegisterInfo(X, ARC::BLINK);
  return X;
}

static MCSubtargetInfo *createARCMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createARCMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createARCMCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TT,
                                     const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new ARCMCAsmInfo(TT);

  // Initial state of the frame pointer is SP.
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, ARC::SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createARCMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new ARCInstPrinter(MAI, MII, MRI);
}

ARCTargetStreamer::ARCTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}
ARCTargetStreamer::~ARCTargetStreamer() = default;

static MCTargetStreamer *createTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint) {
  return new ARCTargetStreamer(S);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARCTargetMC() {
  // Register the MC asm info.
  Target &TheARCTarget = getTheARCTarget();
  RegisterMCAsmInfoFn X(TheARCTarget, createARCMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(TheARCTarget, createARCMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheARCTarget, createARCMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheARCTarget,
                                          createARCMCSubtargetInfo);

  // Register the MCInstPrinter
  TargetRegistry::RegisterMCInstPrinter(TheARCTarget, createARCMCInstPrinter);

  TargetRegistry::RegisterAsmTargetStreamer(TheARCTarget,
                                            createTargetAsmStreamer);
}
