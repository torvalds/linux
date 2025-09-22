//===-- HexagonTargetObjectFile.h -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/MCSectionELF.h"

namespace llvm {
  class Type;

  class HexagonTargetObjectFile : public TargetLoweringObjectFileELF {
  public:
    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

    MCSection *getExplicitSectionGlobal(const GlobalObject *GO,
                                        SectionKind Kind,
                                        const TargetMachine &TM) const override;

    bool isGlobalInSmallSection(const GlobalObject *GO,
                                const TargetMachine &TM) const;

    bool isSmallDataEnabled(const TargetMachine &TM) const;

    unsigned getSmallDataSize() const;

    bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                             const Function &F) const override;

    const Function *getLutUsedFunction(const GlobalObject *GO) const;

  private:
    MCSectionELF *SmallDataSection;
    MCSectionELF *SmallBSSSection;

    unsigned getSmallestAddressableSize(const Type *Ty, const GlobalValue *GV,
        const TargetMachine &TM) const;

    MCSection *selectSmallSectionForGlobal(const GlobalObject *GO,
                                           SectionKind Kind,
                                           const TargetMachine &TM) const;

    MCSection *selectSectionForLookupTable(const GlobalObject *GO,
                                           const TargetMachine &TM,
                                           const Function *Fn) const;
  };

} // namespace llvm

#endif
