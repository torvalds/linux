//===-- NVPTXTargetObjectFile.h - NVPTX Object Info -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXTARGETOBJECTFILE_H

#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

class NVPTXTargetObjectFile : public TargetLoweringObjectFile {
public:
  NVPTXTargetObjectFile() = default;

  ~NVPTXTargetObjectFile() override;

  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override {
    return ReadOnlySection;
  }

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override {
    return DataSection;
  }

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_NVPTXTARGETOBJECTFILE_H
