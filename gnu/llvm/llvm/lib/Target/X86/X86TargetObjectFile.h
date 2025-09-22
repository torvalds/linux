//===-- X86TargetObjectFile.h - X86 Object Info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_X86_X86TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

  /// X86_64MachoTargetObjectFile - This TLOF implementation is used for Darwin
  /// x86-64.
  class X86_64MachoTargetObjectFile : public TargetLoweringObjectFileMachO {
  public:
    const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                          unsigned Encoding,
                                          const TargetMachine &TM,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

    // getCFIPersonalitySymbol - The symbol that gets passed to
    // .cfi_personality.
    MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                      const TargetMachine &TM,
                                      MachineModuleInfo *MMI) const override;

    const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                            const MCSymbol *Sym,
                                            const MCValue &MV, int64_t Offset,
                                            MachineModuleInfo *MMI,
                                            MCStreamer &Streamer) const override;
  };

  /// This implementation is used for X86 ELF targets that don't have a further
  /// specialization (and as a base class for X86_64, which does).
  class X86ELFTargetObjectFile : public TargetLoweringObjectFileELF {
  public:
    X86ELFTargetObjectFile() {
      PLTRelativeVariantKind = MCSymbolRefExpr::VK_PLT;
    }
    /// Describe a TLS variable address within debug info.
    const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const override;
  };

  /// This implementation is used for X86_64 ELF targets, and defers to
  /// X86ELFTargetObjectFile for commonalities with 32-bit targets.
  class X86_64ELFTargetObjectFile : public X86ELFTargetObjectFile {
  public:
    X86_64ELFTargetObjectFile() { SupportIndirectSymViaGOTPCRel = true; }

    const MCExpr *
    getIndirectSymViaGOTPCRel(const GlobalValue *GV, const MCSymbol *Sym,
                              const MCValue &MV, int64_t Offset,
                              MachineModuleInfo *MMI,
                              MCStreamer &Streamer) const override;
  };

} // end namespace llvm

#endif
