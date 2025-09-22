//===-- NVPTXMCAsmInfo.cpp - NVPTX asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the NVPTXMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "NVPTXMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

void NVPTXMCAsmInfo::anchor() {}

NVPTXMCAsmInfo::NVPTXMCAsmInfo(const Triple &TheTriple,
                               const MCTargetOptions &Options) {
  if (TheTriple.getArch() == Triple::nvptx64) {
    CodePointerSize = CalleeSaveStackSlotSize = 8;
  }

  CommentString = "//";

  HasSingleParameterDotFile = false;

  InlineAsmStart = " begin inline asm";
  InlineAsmEnd = " end inline asm";

  SupportsDebugInformation = true;
  // PTX does not allow .align on functions.
  HasFunctionAlignment = false;
  HasDotTypeDotSizeDirective = false;
  // PTX does not allow .hidden or .protected
  HiddenDeclarationVisibilityAttr = HiddenVisibilityAttr = MCSA_Invalid;
  ProtectedVisibilityAttr = MCSA_Invalid;

  Data8bitsDirective = ".b8 ";
  Data16bitsDirective = nullptr; // not supported
  Data32bitsDirective = ".b32 ";
  Data64bitsDirective = ".b64 ";
  ZeroDirective = ".b8";
  AsciiDirective = nullptr; // not supported
  AscizDirective = nullptr; // not supported
  SupportsQuotedNames = false;
  SupportsExtendedDwarfLocDirective = false;
  SupportsSignedData = false;

  PrivateGlobalPrefix = "$L__";
  PrivateLabelPrefix = PrivateGlobalPrefix;

  // @TODO: Can we just disable this?
  WeakDirective = "\t// .weak\t";
  GlobalDirective = "\t// .globl\t";

  UseIntegratedAssembler = false;

  // Avoid using parens for identifiers starting with $ - ptxas does
  // not expect them.
  UseParensForDollarSignNames = false;

  // ptxas does not support DWARF `.file fileno directory filename'
  // syntax as of v11.X.
  EnableDwarfFileDirectoryDefault = false;
}
