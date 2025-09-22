//===-- R600CodeGenPassBuilder.cpp ------ Build R600 CodeGen pipeline -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "R600CodeGenPassBuilder.h"
#include "R600TargetMachine.h"

using namespace llvm;

R600CodeGenPassBuilder::R600CodeGenPassBuilder(
    R600TargetMachine &TM, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC)
    : CodeGenPassBuilder(TM, Opts, PIC) {
  Opt.RequiresCodeGenSCCOrder = true;
}

void R600CodeGenPassBuilder::addPreISel(AddIRPass &addPass) const {
  // TODO: Add passes pre instruction selection.
}

void R600CodeGenPassBuilder::addAsmPrinter(AddMachinePass &addPass,
                                           CreateMCStreamer) const {
  // TODO: Add AsmPrinter.
}

Error R600CodeGenPassBuilder::addInstSelector(AddMachinePass &) const {
  // TODO: Add instruction selector.
  return Error::success();
}
