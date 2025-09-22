//===-- M68kELFTargetObjectFile.cpp - M68k Object Files ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definitions for M68k ELF object file lowering.
///
//===----------------------------------------------------------------------===//

#include "M68kTargetObjectFile.h"

#include "M68kSubtarget.h"
#include "M68kTargetMachine.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static cl::opt<unsigned> SSThreshold(
    "m68k-ssection-threshold", cl::Hidden,
    cl::desc("Small data and bss section threshold size (default=8)"),
    cl::init(8));

void M68kELFTargetObjectFile::Initialize(MCContext &Ctx,
                                         const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  InitializeELF(TM.Options.UseInitArray);

  this->TM = &static_cast<const M68kTargetMachine &>(TM);

  // FIXME do we need `.sdata` and `.sbss` explicitly?
  SmallDataSection = getContext().getELFSection(
      ".sdata", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);

  SmallBSSSection = getContext().getELFSection(".sbss", ELF::SHT_NOBITS,
                                               ELF::SHF_WRITE | ELF::SHF_ALLOC);
}
