//===-- PPCMCAsmInfo.cpp - PPC asm properties -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the MCAsmInfoDarwin properties.
//
//===----------------------------------------------------------------------===//

#include "PPCMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

void PPCELFMCAsmInfo::anchor() { }

PPCELFMCAsmInfo::PPCELFMCAsmInfo(bool is64Bit, const Triple& T) {
  // FIXME: This is not always needed. For example, it is not needed in the
  // v2 abi.
  NeedsLocalForSize = true;

  if (is64Bit) {
    CodePointerSize = CalleeSaveStackSlotSize = 8;
  }
  IsLittleEndian =
      T.getArch() == Triple::ppc64le || T.getArch() == Triple::ppcle;

  // ".comm align is in bytes but .align is pow-2."
  AlignmentIsInBytes = false;

  CommentString = "#";

  // Uses '.section' before '.bss' directive
  UsesELFSectionDirectiveForBSS = true;

  // Debug Information
  SupportsDebugInformation = true;

  DollarIsPC = true;

  // Set up DWARF directives
  MinInstAlignment = 4;

  // Exceptions handling
  ExceptionsType = ExceptionHandling::DwarfCFI;

  ZeroDirective = "\t.space\t";
  Data64bitsDirective = is64Bit ? "\t.quad\t" : nullptr;
  AssemblerDialect = 1;           // New-Style mnemonics.
  LCOMMDirectiveAlignmentType = LCOMM::ByteAlignment;
}

void PPCXCOFFMCAsmInfo::anchor() {}

PPCXCOFFMCAsmInfo::PPCXCOFFMCAsmInfo(bool Is64Bit, const Triple &T) {
  if (T.getArch() == Triple::ppc64le || T.getArch() == Triple::ppcle)
    report_fatal_error("XCOFF is not supported for little-endian targets");
  CodePointerSize = CalleeSaveStackSlotSize = Is64Bit ? 8 : 4;

  // A size of 8 is only supported by the assembler under 64-bit.
  Data64bitsDirective = Is64Bit ? "\t.vbyte\t8, " : nullptr;

  // Debug Information
  SupportsDebugInformation = true;

  // Set up DWARF directives
  MinInstAlignment = 4;

  // Support $ as PC in inline asm
  DollarIsPC = true;
}
