//===-- SparcTargetStreamer.cpp - Sparc Target Streamer Methods -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Sparc specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "SparcTargetStreamer.h"
#include "SparcInstPrinter.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

// pin vtable to this file
SparcTargetStreamer::SparcTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

void SparcTargetStreamer::anchor() {}

SparcTargetAsmStreamer::SparcTargetAsmStreamer(MCStreamer &S,
                                               formatted_raw_ostream &OS)
    : SparcTargetStreamer(S), OS(OS) {}

void SparcTargetAsmStreamer::emitSparcRegisterIgnore(unsigned reg) {
  OS << "\t.register "
     << "%" << StringRef(SparcInstPrinter::getRegisterName(reg)).lower()
     << ", #ignore\n";
}

void SparcTargetAsmStreamer::emitSparcRegisterScratch(unsigned reg) {
  OS << "\t.register "
     << "%" << StringRef(SparcInstPrinter::getRegisterName(reg)).lower()
     << ", #scratch\n";
}

SparcTargetELFStreamer::SparcTargetELFStreamer(MCStreamer &S)
    : SparcTargetStreamer(S) {}

MCELFStreamer &SparcTargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}
