//===-- SPIRVTargetObjectFile.h - SPIRV Object Info -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVTARGETOBJECTFILE_H

#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

class SPIRVTargetObjectFile : public TargetLoweringObjectFile {
public:
  ~SPIRVTargetObjectFile() override;

  void Initialize(MCContext &ctx, const TargetMachine &TM) override {
    TargetLoweringObjectFile::Initialize(ctx, TM);
  }
  // All words in a SPIR-V module (excepting the first 5 ones) are a linear
  // sequence of instructions in a specific order. We put all the instructions
  // in the single text section.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override {
    return TextSection;
  }
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override {
    return TextSection;
  }
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override {
    return TextSection;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVTARGETOBJECTFILE_H
