//===-- MCTargetDesc/AMDGPUMCAsmInfo.cpp - Assembly Info ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
//===----------------------------------------------------------------------===//

#include "AMDGPUMCAsmInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

AMDGPUMCAsmInfo::AMDGPUMCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options) {
  CodePointerSize = (TT.getArch() == Triple::amdgcn) ? 8 : 4;
  StackGrowsUp = true;
  HasSingleParameterDotFile = false;
  //===------------------------------------------------------------------===//
  MinInstAlignment = 4;

  // This is the maximum instruction encoded size for gfx10. With a known
  // subtarget, it can be reduced to 8 bytes.
  MaxInstLength = (TT.getArch() == Triple::amdgcn) ? 20 : 16;
  SeparatorString = "\n";
  CommentString = ";";
  InlineAsmStart = ";#ASMSTART";
  InlineAsmEnd = ";#ASMEND";

  //===--- Data Emission Directives -------------------------------------===//
  UsesELFSectionDirectiveForBSS = true;

  //===--- Global Variable Emission Directives --------------------------===//
  HasAggressiveSymbolFolding = true;
  COMMDirectiveAlignmentIsInBytes = false;
  HasNoDeadStrip = true;
  //===--- Dwarf Emission Directives -----------------------------------===//
  SupportsDebugInformation = true;
  UsesCFIWithoutEH = true;
  DwarfRegNumForCFI = true;

  UseIntegratedAssembler = false;
}

bool AMDGPUMCAsmInfo::shouldOmitSectionDirective(StringRef SectionName) const {
  return SectionName == ".hsatext" || SectionName == ".hsadata_global_agent" ||
         SectionName == ".hsadata_global_program" ||
         SectionName == ".hsarodata_readonly_agent" ||
         MCAsmInfo::shouldOmitSectionDirective(SectionName);
}

unsigned AMDGPUMCAsmInfo::getMaxInstLength(const MCSubtargetInfo *STI) const {
  if (!STI || STI->getTargetTriple().getArch() == Triple::r600)
    return MaxInstLength;

  // Maximum for NSA encoded images
  if (STI->hasFeature(AMDGPU::FeatureNSAEncoding))
    return 20;

  // 64-bit instruction with 32-bit literal.
  if (STI->hasFeature(AMDGPU::FeatureVOP3Literal))
    return 12;

  return 8;
}
