//===-- VETargetStreamer.cpp - VE Target Streamer Methods -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides VE specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "VETargetStreamer.h"
#include "VEInstPrinter.h"
#include "llvm/MC/MCRegister.h"

using namespace llvm;

// pin vtable to this file
VETargetStreamer::VETargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

void VETargetStreamer::anchor() {}

VETargetAsmStreamer::VETargetAsmStreamer(MCStreamer &S,
                                         formatted_raw_ostream &OS)
    : VETargetStreamer(S), OS(OS) {}

void VETargetAsmStreamer::emitVERegisterIgnore(unsigned reg) {
  OS << "\t.register "
     << "%" << StringRef(VEInstPrinter::getRegisterName(reg)).lower()
     << ", #ignore\n";
}

void VETargetAsmStreamer::emitVERegisterScratch(unsigned reg) {
  OS << "\t.register "
     << "%" << StringRef(VEInstPrinter::getRegisterName(reg)).lower()
     << ", #scratch\n";
}

VETargetELFStreamer::VETargetELFStreamer(MCStreamer &S) : VETargetStreamer(S) {}

MCELFStreamer &VETargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}
