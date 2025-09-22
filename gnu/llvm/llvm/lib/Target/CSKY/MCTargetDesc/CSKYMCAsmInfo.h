//===-- CSKYMCAsmInfo.h - CSKY Asm Info ------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the CSKYMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCASMINFO_H
#define LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class CSKYMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit CSKYMCAsmInfo(const Triple &TargetTriple);
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCASMINFO_H
