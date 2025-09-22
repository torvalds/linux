//===-- XCoreTargetObjectFile.h - XCore Object Info -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORETARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_XCORE_XCORETARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

static const unsigned CodeModelLargeSize = 256;

  class XCoreTargetObjectFile : public TargetLoweringObjectFileELF {
    MCSection *BSSSectionLarge;
    MCSection *DataSectionLarge;
    MCSection *ReadOnlySectionLarge;
    MCSection *DataRelROSectionLarge;

  public:
    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                        const TargetMachine &TM) const override;

    MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

    MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                     const Constant *C,
                                     Align &Alignment) const override;
  };
} // end namespace llvm

#endif
