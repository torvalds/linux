//===-- PPCTargetObjectFile.h - PPC Object Info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_POWERPC_PPCTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

  /// PPC64LinuxTargetObjectFile - This implementation is used for
  /// 64-bit PowerPC Linux.
  class PPC64LinuxTargetObjectFile : public TargetLoweringObjectFileELF {

    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

    /// Describe a TLS variable address within debug info.
    const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const override;
  };

}  // end namespace llvm

#endif
