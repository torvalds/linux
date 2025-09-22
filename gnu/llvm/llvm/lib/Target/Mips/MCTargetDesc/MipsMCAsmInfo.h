//===-- MipsMCAsmInfo.h - Mips Asm Info ------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MipsMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSMCASMINFO_H
#define LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class MipsMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit MipsMCAsmInfo(const Triple &TheTriple,
                         const MCTargetOptions &Options);
};

} // namespace llvm

#endif
