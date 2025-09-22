//===-- VEMCTargetDesc.cpp - VE Target Descriptions -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides VE specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "VEMCTargetDesc.h"
#include "TargetInfo/VETargetInfo.h"
#include "VEInstPrinter.h"
#include "VEMCAsmInfo.h"
#include "VETargetStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "VEGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "VEGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "VEGenRegisterInfo.inc"

static MCAsmInfo *createVEMCAsmInfo(const MCRegisterInfo &MRI, const Triple &TT,
                                    const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new VEELFMCAsmInfo(TT);
  unsigned Reg = MRI.getDwarfRegNum(VE::SX11, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, Reg, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstrInfo *createVEMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitVEMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createVEMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitVEMCRegisterInfo(X, VE::SX10);
  return X;
}

static MCSubtargetInfo *createVEMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                                StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createVEMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCTargetStreamer *
createObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new VETargetELFStreamer(S);
}

static MCTargetStreamer *createTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint) {
  return new VETargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new VETargetStreamer(S);
}

static MCInstPrinter *createVEMCInstPrinter(const Triple &T,
                                            unsigned SyntaxVariant,
                                            const MCAsmInfo &MAI,
                                            const MCInstrInfo &MII,
                                            const MCRegisterInfo &MRI) {
  return new VEInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVETargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(getTheVETarget(), createVEMCAsmInfo);

  for (Target *T : {&getTheVETarget()}) {
    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createVEMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createVEMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createVEMCSubtargetInfo);

    // Register the MC Code Emitter.
    TargetRegistry::RegisterMCCodeEmitter(*T, createVEMCCodeEmitter);

    // Register the asm backend.
    TargetRegistry::RegisterMCAsmBackend(*T, createVEAsmBackend);

    // Register the object target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createObjectTargetStreamer);

    // Register the asm streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createTargetAsmStreamer);

    // Register the null streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createNullTargetStreamer);

    // Register the MCInstPrinter
    TargetRegistry::RegisterMCInstPrinter(*T, createVEMCInstPrinter);
  }
}
