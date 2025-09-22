//===-- llvm/Target/ARMTargetObjectFile.h - ARM Object Info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_ARM_ARMTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCRegister.h"

namespace llvm {

class ARMElfTargetObjectFile : public TargetLoweringObjectFileELF {
public:
  ARMElfTargetObjectFile() {
    PLTRelativeVariantKind = MCSymbolRefExpr::VK_ARM_PREL31;
    SupportIndirectSymViaGOTPCRel = true;
  }

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  MCRegister getStaticBase() const override;

  const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                          const MCSymbol *Sym,
                                          const MCValue &MV, int64_t Offset,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

  const MCExpr *getIndirectSymViaRWPI(const MCSymbol *Sym) const override;

  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  /// Describe a TLS variable address within debug info.
  const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMTARGETOBJECTFILE_H
