//===-- AMDGPUHSATargetObjectFile.cpp - AMDGPU Object Files ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetObjectFile.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
// Generic Object File
//===----------------------------------------------------------------------===//

MCSection *AMDGPUTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (Kind.isReadOnly() && AMDGPU::isReadOnlySegment(GO) &&
      AMDGPU::shouldEmitConstantsToTextSection(TM.getTargetTriple()))
    return TextSection;

  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

MCSection *AMDGPUTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind SK, const TargetMachine &TM) const {
  // Set metadata access for the explicit section
  StringRef SectionName = GO->getSection();
  if (SectionName.starts_with(".AMDGPU.comment."))
    SK = SectionKind::getMetadata();

  return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, SK, TM);
}
