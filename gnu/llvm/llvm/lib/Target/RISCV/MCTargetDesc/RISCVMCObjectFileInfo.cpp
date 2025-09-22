//===-- RISCVMCObjectFileInfo.cpp - RISC-V object file properties ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the RISCVMCObjectFileInfo properties.
//
//===----------------------------------------------------------------------===//

#include "RISCVMCObjectFileInfo.h"
#include "RISCVMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

unsigned
RISCVMCObjectFileInfo::getTextSectionAlignment(const MCSubtargetInfo &STI) {
  bool RVC = STI.hasFeature(RISCV::FeatureStdExtC) ||
             STI.hasFeature(RISCV::FeatureStdExtZca);
  return RVC ? 2 : 4;
}

unsigned RISCVMCObjectFileInfo::getTextSectionAlignment() const {
  return getTextSectionAlignment(*getContext().getSubtargetInfo());
}
