//===-- XCoreMCTargetDesc.cpp - XCore Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides XCore specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/XCoreMCTargetDesc.h"
#include "MCTargetDesc/XCoreInstPrinter.h"
#include "MCTargetDesc/XCoreMCAsmInfo.h"
#include "TargetInfo/XCoreTargetInfo.h"
#include "XCoreTargetStreamer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "XCoreGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "XCoreGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "XCoreGenRegisterInfo.inc"

static MCInstrInfo *createXCoreMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitXCoreMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createXCoreMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitXCoreMCRegisterInfo(X, XCore::LR);
  return X;
}

static MCSubtargetInfo *
createXCoreMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createXCoreMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCAsmInfo *createXCoreMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new XCoreMCAsmInfo(TT);

  // Initial state of the frame pointer is SP.
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, XCore::SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createXCoreMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new XCoreInstPrinter(MAI, MII, MRI);
}

XCoreTargetStreamer::XCoreTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

XCoreTargetStreamer::~XCoreTargetStreamer() = default;

namespace {

class XCoreTargetAsmStreamer : public XCoreTargetStreamer {
  formatted_raw_ostream &OS;

public:
  XCoreTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitCCTopData(StringRef Name) override;
  void emitCCTopFunction(StringRef Name) override;
  void emitCCBottomData(StringRef Name) override;
  void emitCCBottomFunction(StringRef Name) override;
};

} // end anonymous namespace

XCoreTargetAsmStreamer::XCoreTargetAsmStreamer(MCStreamer &S,
                                               formatted_raw_ostream &OS)
    : XCoreTargetStreamer(S), OS(OS) {}

void XCoreTargetAsmStreamer::emitCCTopData(StringRef Name) {
  OS << "\t.cc_top " << Name << ".data," << Name << '\n';
}

void XCoreTargetAsmStreamer::emitCCTopFunction(StringRef Name) {
  OS << "\t.cc_top " << Name << ".function," << Name << '\n';
}

void XCoreTargetAsmStreamer::emitCCBottomData(StringRef Name) {
  OS << "\t.cc_bottom " << Name << ".data\n";
}

void XCoreTargetAsmStreamer::emitCCBottomFunction(StringRef Name) {
  OS << "\t.cc_bottom " << Name << ".function\n";
}

static MCTargetStreamer *createTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint) {
  return new XCoreTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new XCoreTargetStreamer(S);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXCoreTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(getTheXCoreTarget(), createXCoreMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheXCoreTarget(),
                                      createXCoreMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheXCoreTarget(),
                                    createXCoreMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(getTheXCoreTarget(),
                                          createXCoreMCSubtargetInfo);

  // Register the MCInstPrinter
  TargetRegistry::RegisterMCInstPrinter(getTheXCoreTarget(),
                                        createXCoreMCInstPrinter);

  TargetRegistry::RegisterAsmTargetStreamer(getTheXCoreTarget(),
                                            createTargetAsmStreamer);

  TargetRegistry::RegisterNullTargetStreamer(getTheXCoreTarget(),
                                             createNullTargetStreamer);
}
