//===-- AMDGPUTargetObjectFile.h - AMDGPU  Object Info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the AMDGPU-specific subclass of
/// TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

class AMDGPUTargetObjectFile : public TargetLoweringObjectFileELF {
  public:
    MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;
    MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                        const TargetMachine &TM) const override;
};

} // end namespace llvm

#endif
