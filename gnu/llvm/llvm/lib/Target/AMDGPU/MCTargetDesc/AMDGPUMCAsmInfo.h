//===-- MCTargetDesc/AMDGPUMCAsmInfo.h - AMDGPU MCAsm Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCASMINFO_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"
namespace llvm {

class Triple;

// If you need to create another MCAsmInfo class, which inherits from MCAsmInfo,
// you will need to make sure your new class sets PrivateGlobalPrefix to
// a prefix that won't appear in a function name.  The default value
// for PrivateGlobalPrefix is 'L', so it will consider any function starting
// with 'L' as a local symbol.
class AMDGPUMCAsmInfo : public MCAsmInfoELF {
public:
  explicit AMDGPUMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
  bool shouldOmitSectionDirective(StringRef SectionName) const override;
  unsigned getMaxInstLength(const MCSubtargetInfo *STI) const override;
};
} // namespace llvm
#endif
