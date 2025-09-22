//===-- HexagonTargetAsmInfo.h - Hexagon asm properties --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the HexagonMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCASMINFO_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class HexagonMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit HexagonMCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
