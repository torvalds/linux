//===-- llvm/Target/MipsTargetObjectFile.h - Mips Object Info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MIPS_MIPSTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {
class MipsTargetMachine;
  class MipsTargetObjectFile : public TargetLoweringObjectFileELF {
    MCSection *SmallDataSection;
    MCSection *SmallBSSSection;
    const MipsTargetMachine *TM;

    bool IsGlobalInSmallSection(const GlobalObject *GO, const TargetMachine &TM,
                                SectionKind Kind) const;
    bool IsGlobalInSmallSectionImpl(const GlobalObject *GO,
                                    const TargetMachine &TM) const;
  public:

    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    /// Return true if this global address should be placed into small data/bss
    /// section.
    bool IsGlobalInSmallSection(const GlobalObject *GO,
                                const TargetMachine &TM) const;

    MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

    /// Return true if this constant should be placed into small data section.
    bool IsConstantInSmallSection(const DataLayout &DL, const Constant *CN,
                                  const TargetMachine &TM) const;

    MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                     const Constant *C,
                                     Align &Alignment) const override;
    /// Describe a TLS variable address within debug info.
    const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const override;
  };
} // end namespace llvm

#endif
