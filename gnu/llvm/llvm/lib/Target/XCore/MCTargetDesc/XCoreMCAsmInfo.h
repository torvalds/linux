//===-- XCoreMCAsmInfo.h - XCore asm properties ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the XCoreMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCASMINFO_H
#define LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class XCoreMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit XCoreMCAsmInfo(const Triple &TT);
};

} // namespace llvm

#endif
