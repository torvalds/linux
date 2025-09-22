//===-- M68kMCAsmInfo.h - M68k Asm Info -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the M68k MCAsmInfo properties.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCASMINFO_H
#define LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class M68kELFMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit M68kELFMCAsmInfo(const Triple &Triple);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCASMINFO_H
