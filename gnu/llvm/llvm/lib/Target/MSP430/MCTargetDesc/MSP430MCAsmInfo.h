//===-- MSP430MCAsmInfo.h - MSP430 asm properties --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MSP430MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCASMINFO_H
#define LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class MSP430MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit MSP430MCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
