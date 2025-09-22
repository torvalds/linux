//===-- PPCMCAsmInfo.h - PPC asm properties --------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the PowerPC MCAsmInfo classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCASMINFO_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCAsmInfoXCOFF.h"

namespace llvm {
class Triple;

class PPCELFMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit PPCELFMCAsmInfo(bool is64Bit, const Triple &);
};

class PPCXCOFFMCAsmInfo : public MCAsmInfoXCOFF {
  void anchor() override;

public:
  explicit PPCXCOFFMCAsmInfo(bool is64Bit, const Triple &);
};

} // namespace llvm

#endif
