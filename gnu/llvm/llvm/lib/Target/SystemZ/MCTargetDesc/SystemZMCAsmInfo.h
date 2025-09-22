//====-- SystemZMCAsmInfo.h - SystemZ asm properties -----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCASMINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCAsmInfoGOFF.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Triple;
enum SystemZAsmDialect { AD_ATT = 0, AD_HLASM = 1 };

class SystemZMCAsmInfoELF : public MCAsmInfoELF {
public:
  explicit SystemZMCAsmInfoELF(const Triple &TT);
};

class SystemZMCAsmInfoGOFF : public MCAsmInfoGOFF {
public:
  explicit SystemZMCAsmInfoGOFF(const Triple &TT);
  bool isAcceptableChar(char C) const override;
};

} // end namespace llvm

#endif
