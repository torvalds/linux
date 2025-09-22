//===- ARCAsmPrinter.cpp - ARC LLVM assembly writer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GNU format ARC assembly language.
//
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "ARCMCInstLower.h"
#include "ARCSubtarget.h"
#include "ARCTargetMachine.h"
#include "MCTargetDesc/ARCInstPrinter.h"
#include "TargetInfo/ARCTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class ARCAsmPrinter : public AsmPrinter {
  ARCMCInstLower MCInstLowering;

public:
  explicit ARCAsmPrinter(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)),
        MCInstLowering(&OutContext, *this) {}

  StringRef getPassName() const override { return "ARC Assembly Printer"; }
  void emitInstruction(const MachineInstr *MI) override;

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

void ARCAsmPrinter::emitInstruction(const MachineInstr *MI) {
  ARC_MC::verifyInstructionPredicates(MI->getOpcode(),
                                      getSubtargetInfo().getFeatureBits());

  SmallString<128> Str;
  raw_svector_ostream O(Str);

  switch (MI->getOpcode()) {
  case ARC::DBG_VALUE:
    llvm_unreachable("Should be handled target independently");
    break;
  }

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

bool ARCAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // Functions are 4-byte aligned.
  MF.ensureAlignment(Align(4));
  return AsmPrinter::runOnMachineFunction(MF);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARCAsmPrinter() {
  RegisterAsmPrinter<ARCAsmPrinter> X(getTheARCTarget());
}
