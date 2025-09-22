//===-- NVPTXMCTargetDesc.cpp - NVPTX Target Descriptions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides NVPTX specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "NVPTXMCTargetDesc.h"
#include "NVPTXInstPrinter.h"
#include "NVPTXMCAsmInfo.h"
#include "NVPTXTargetStreamer.h"
#include "TargetInfo/NVPTXTargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "NVPTXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "NVPTXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "NVPTXGenRegisterInfo.inc"

static MCInstrInfo *createNVPTXMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitNVPTXMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createNVPTXMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  // PTX does not have a return address register.
  InitNVPTXMCRegisterInfo(X, 0);
  return X;
}

static MCSubtargetInfo *
createNVPTXMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createNVPTXMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCInstPrinter *createNVPTXMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new NVPTXInstPrinter(MAI, MII, MRI);
  return nullptr;
}

static MCTargetStreamer *createTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &,
                                                 MCInstPrinter *) {
  return new NVPTXAsmTargetStreamer(S);
}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new NVPTXTargetStreamer(S);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeNVPTXTargetMC() {
  for (Target *T : {&getTheNVPTXTarget32(), &getTheNVPTXTarget64()}) {
    // Register the MC asm info.
    RegisterMCAsmInfo<NVPTXMCAsmInfo> X(*T);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createNVPTXMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createNVPTXMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createNVPTXMCSubtargetInfo);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createNVPTXMCInstPrinter);

    // Register the MCTargetStreamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createTargetAsmStreamer);

    // Register the MCTargetStreamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createNullTargetStreamer);
  }
}
