//===-- SystemZMCAsmInfo.cpp - SystemZ asm properties ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZMCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"

using namespace llvm;

SystemZMCAsmInfoELF::SystemZMCAsmInfoELF(const Triple &TT) {
  AssemblerDialect = AD_ATT;
  CalleeSaveStackSlotSize = 8;
  CodePointerSize = 8;
  Data64bitsDirective = "\t.quad\t";
  ExceptionsType = ExceptionHandling::DwarfCFI;
  IsLittleEndian = false;
  MaxInstLength = 6;
  SupportsDebugInformation = true;
  UsesELFSectionDirectiveForBSS = true;
  ZeroDirective = "\t.space\t";
}

SystemZMCAsmInfoGOFF::SystemZMCAsmInfoGOFF(const Triple &TT) {
  AllowAdditionalComments = false;
  AllowAtInName = true;
  AllowAtAtStartOfIdentifier = true;
  AllowDollarAtStartOfIdentifier = true;
  AllowHashAtStartOfIdentifier = true;
  AssemblerDialect = AD_HLASM;
  CalleeSaveStackSlotSize = 8;
  CodePointerSize = 8;
  CommentString = "*";
  DotIsPC = false;
  EmitGNUAsmStartIndentationMarker = false;
  EmitLabelsInUpperCase = true;
  ExceptionsType = ExceptionHandling::ZOS;
  IsLittleEndian = false;
  MaxInstLength = 6;
  RestrictCommentStringToStartOfStatement = true;
  StarIsPC = true;
  SupportsDebugInformation = true;
}

bool SystemZMCAsmInfoGOFF::isAcceptableChar(char C) const {
  return MCAsmInfo::isAcceptableChar(C) || C == '#';
}
