//===-- SPIRVMCAsmInfo.h - SPIR-V asm properties --------------*- C++ -*--====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the SPIRVMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "SPIRVMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

SPIRVMCAsmInfo::SPIRVMCAsmInfo(const Triple &TT,
                               const MCTargetOptions &Options) {
  IsLittleEndian = true;

  HasSingleParameterDotFile = false;
  HasDotTypeDotSizeDirective = false;

  MinInstAlignment = 4;

  CodePointerSize = 4;
  CommentString = ";";
  HasFunctionAlignment = false;
}

bool SPIRVMCAsmInfo::shouldOmitSectionDirective(StringRef SectionName) const {
  return true;
}
