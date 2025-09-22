//===-- ARMMCAsmInfo.h - ARM asm properties --------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the ARMMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCASMINFO_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCASMINFO_H

#include "llvm/MC/MCAsmInfoCOFF.h"
#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class ARMMCAsmInfoDarwin : public MCAsmInfoDarwin {
  virtual void anchor();

public:
  explicit ARMMCAsmInfoDarwin(const Triple &TheTriple);
};

class ARMELFMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit ARMELFMCAsmInfo(const Triple &TT);

  void setUseIntegratedAssembler(bool Value) override;
};

class ARMCOFFMCAsmInfoMicrosoft : public MCAsmInfoMicrosoft {
  void anchor() override;

public:
  explicit ARMCOFFMCAsmInfoMicrosoft();
};

class ARMCOFFMCAsmInfoGNU : public MCAsmInfoGNUCOFF {
  void anchor() override;

public:
  explicit ARMCOFFMCAsmInfoGNU();
};

} // namespace llvm

#endif
