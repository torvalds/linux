//===-- MSP430MCAsmInfo.cpp - MSP430 asm properties -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the MSP430MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "MSP430MCAsmInfo.h"
using namespace llvm;

void MSP430MCAsmInfo::anchor() { }

MSP430MCAsmInfo::MSP430MCAsmInfo(const Triple &TT) {
  // Since MSP430-GCC already generates 32-bit DWARF information, we will
  // also store 16-bit pointers as 32-bit pointers in DWARF, because using
  // 32-bit DWARF pointers is already a working and tested path for LLDB
  // as well.
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 2;

  CommentString = ";";
  SeparatorString = "{";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;

  SupportsDebugInformation = true;

  ExceptionsType = ExceptionHandling::DwarfCFI;
}
