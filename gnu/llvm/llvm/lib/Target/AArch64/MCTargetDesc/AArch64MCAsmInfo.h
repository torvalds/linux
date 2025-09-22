//=====-- AArch64MCAsmInfo.h - AArch64 asm properties ---------*- C++ -*--====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the AArch64MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCASMINFO_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCASMINFO_H

#include "llvm/MC/MCAsmInfoCOFF.h"
#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class MCStreamer;
class Triple;

struct AArch64MCAsmInfoDarwin : public MCAsmInfoDarwin {
  explicit AArch64MCAsmInfoDarwin(bool IsILP32);
  const MCExpr *
  getExprForPersonalitySymbol(const MCSymbol *Sym, unsigned Encoding,
                              MCStreamer &Streamer) const override;
};

struct AArch64MCAsmInfoELF : public MCAsmInfoELF {
  explicit AArch64MCAsmInfoELF(const Triple &T);
};

struct AArch64MCAsmInfoMicrosoftCOFF : public MCAsmInfoMicrosoft {
  explicit AArch64MCAsmInfoMicrosoftCOFF();
};

struct AArch64MCAsmInfoGNUCOFF : public MCAsmInfoGNUCOFF {
  explicit AArch64MCAsmInfoGNUCOFF();
};

} // namespace llvm

#endif
