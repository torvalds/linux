//===- MCAsmInfoELF.cpp - ELF asm properties ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on ELF-based targets
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"

using namespace llvm;

void MCAsmInfoELF::anchor() {}

MCSection *MCAsmInfoELF::getNonexecutableStackSection(MCContext &Ctx) const {
  // Solaris doesn't know/doesn't care about .note.GNU-stack sections, so
  // don't emit them.
  if (Ctx.getTargetTriple().isOSSolaris())
    return nullptr;
  return Ctx.getELFSection(".note.GNU-stack", ELF::SHT_PROGBITS, 0);
}

MCAsmInfoELF::MCAsmInfoELF() {
  HasIdentDirective = false;
  WeakRefDirective = "\t.weak\t";
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";
}
