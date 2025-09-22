//===-- RISCVMCObjectFileInfo.h - RISC-V object file Info ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the RISCVMCObjectFileInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVMCOBJECTFILEINFO_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVMCOBJECTFILEINFO_H

#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {

class RISCVMCObjectFileInfo : public MCObjectFileInfo {
public:
  static unsigned getTextSectionAlignment(const MCSubtargetInfo &STI);
  unsigned getTextSectionAlignment() const override;
};

} // namespace llvm

#endif
