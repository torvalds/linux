//===-- AVRMCTargetDesc.cpp - AVR Target Descriptions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides AVR specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AVRMCTargetDesc.h"
#include "AVRELFStreamer.h"
#include "AVRInstPrinter.h"
#include "AVRMCAsmInfo.h"
#include "AVRMCELFStreamer.h"
#include "AVRTargetStreamer.h"
#include "TargetInfo/AVRTargetInfo.h"

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "AVRGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AVRGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AVRGenRegisterInfo.inc"

using namespace llvm;

MCInstrInfo *llvm::createAVRMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAVRMCInstrInfo(X);

  return X;
}

static MCRegisterInfo *createAVRMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAVRMCRegisterInfo(X, 0);

  return X;
}

static MCSubtargetInfo *createAVRMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createAVRMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createAVRMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0) {
    return new AVRInstPrinter(MAI, MII, MRI);
  }

  return nullptr;
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createELFStreamer(Context, std::move(MAB), std::move(OW),
                           std::move(Emitter));
}

static MCTargetStreamer *
createAVRObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new AVRELFStreamer(S, STI);
}

static MCTargetStreamer *createMCAsmTargetStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS,
                                                   MCInstPrinter *InstPrint) {
  return new AVRTargetAsmStreamer(S);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAVRTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfo<AVRMCAsmInfo> X(getTheAVRTarget());

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheAVRTarget(), createAVRMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheAVRTarget(), createAVRMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(getTheAVRTarget(),
                                          createAVRMCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(getTheAVRTarget(),
                                        createAVRMCInstPrinter);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheAVRTarget(),
                                        createAVRMCCodeEmitter);

  // Register the obj streamer
  TargetRegistry::RegisterELFStreamer(getTheAVRTarget(), createMCStreamer);

  // Register the obj target streamer.
  TargetRegistry::RegisterObjectTargetStreamer(getTheAVRTarget(),
                                               createAVRObjectTargetStreamer);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(getTheAVRTarget(),
                                            createMCAsmTargetStreamer);

  // Register the asm backend (as little endian).
  TargetRegistry::RegisterMCAsmBackend(getTheAVRTarget(), createAVRAsmBackend);
}
