//===-- NVPTXMCAsmInfo.h - NVPTX asm properties ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVPTXMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Triple;

class NVPTXMCAsmInfo : public MCAsmInfo {
  virtual void anchor();

public:
  explicit NVPTXMCAsmInfo(const Triple &TheTriple,
                          const MCTargetOptions &Options);

  /// Return true if the .section directive should be omitted when
  /// emitting \p SectionName.  For example:
  ///
  /// shouldOmitSectionDirective(".text")
  ///
  /// returns false => .section .text,#alloc,#execinstr
  /// returns true  => .text
  bool shouldOmitSectionDirective(StringRef SectionName) const override {
    return true;
  }
};
} // namespace llvm

#endif
