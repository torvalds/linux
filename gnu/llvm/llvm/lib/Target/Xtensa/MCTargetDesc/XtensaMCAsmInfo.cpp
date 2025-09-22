//===-- XtensaMCAsmInfo.cpp - Xtensa Asm Properties -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the XtensaMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "XtensaMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

XtensaMCAsmInfo::XtensaMCAsmInfo(const Triple &TT) {
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 4;
  PrivateGlobalPrefix = ".L";
  CommentString = "#";
  ZeroDirective = "\t.space\t";
  Data64bitsDirective = "\t.quad\t";
  GlobalDirective = "\t.global\t";
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  AlignmentIsInBytes = false;
}
