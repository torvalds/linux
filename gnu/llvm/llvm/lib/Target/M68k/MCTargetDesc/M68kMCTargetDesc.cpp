//===-- M68kMCTargetDesc.cpp - M68k Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides M68k target specific descriptions.
///
//===----------------------------------------------------------------------===//

#include "M68kMCTargetDesc.h"
#include "M68kInstPrinter.h"
#include "M68kMCAsmInfo.h"
#include "TargetInfo/M68kTargetInfo.h"

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "M68kGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "M68kGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "M68kGenRegisterInfo.inc"

// TODO Implement feature set parsing logics
static std::string ParseM68kTriple(const Triple &TT, StringRef CPU) {
  return "";
}

static MCInstrInfo *createM68kMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitM68kMCInstrInfo(X); // defined in M68kGenInstrInfo.inc
  return X;
}

static MCRegisterInfo *createM68kMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitM68kMCRegisterInfo(X, llvm::M68k::A0, 0, 0, llvm::M68k::PC);
  return X;
}

static MCSubtargetInfo *createM68kMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  std::string ArchFS = ParseM68kTriple(TT, CPU);
  if (!FS.empty()) {
    if (!ArchFS.empty()) {
      ArchFS = (ArchFS + "," + FS).str();
    } else {
      ArchFS = FS.str();
    }
  }
  return createM68kMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, ArchFS);
}

static MCAsmInfo *createM68kMCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &TO) {
  MCAsmInfo *MAI = new M68kELFMCAsmInfo(TT);

  // Initialize initial frame state.
  // Calculate amount of bytes used for return address storing
  int StackGrowth = -4;

  // Initial state of the frame pointer is SP+StackGrowth.
  // TODO: Add tests for `cfi_*` directives
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(llvm::M68k::SP, true), -StackGrowth);
  MAI->addInitialFrameState(Inst);

  // Add return address to move list
  Inst = MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(M68k::PC, true), StackGrowth);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCRelocationInfo *createM68kMCRelocationInfo(const Triple &TheTriple,
                                                    MCContext &Ctx) {
  // Default to the stock relocation info.
  return llvm::createMCRelocationInfo(TheTriple, Ctx);
}

static MCInstPrinter *createM68kMCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new M68kInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kTargetMC() {
  Target &T = getTheM68kTarget();

  // Register the MC asm info.
  RegisterMCAsmInfoFn X(T, createM68kMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(T, createM68kMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(T, createM68kMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(T, createM68kMCSubtargetInfo);

  // Register the code emitter.
  TargetRegistry::RegisterMCCodeEmitter(T, createM68kMCCodeEmitter);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(T, createM68kMCInstPrinter);

  // Register the MC relocation info.
  TargetRegistry::RegisterMCRelocationInfo(T, createM68kMCRelocationInfo);

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(T, createM68kAsmBackend);
}
