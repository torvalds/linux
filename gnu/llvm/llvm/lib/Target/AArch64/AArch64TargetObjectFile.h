//===-- AArch64TargetObjectFile.h - AArch64 Object Info -*- C++ ---------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64TARGETOBJECTFILE_H

#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

/// This implementation is used for AArch64 ELF targets (Linux in particular).
class AArch64_ELFTargetObjectFile : public TargetLoweringObjectFileELF {
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

public:
  AArch64_ELFTargetObjectFile() {
    PLTRelativeVariantKind = MCSymbolRefExpr::VK_PLT;
    SupportIndirectSymViaGOTPCRel = true;
  }

  const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                          const MCSymbol *Sym,
                                          const MCValue &MV, int64_t Offset,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

  MCSymbol *getAuthPtrSlotSymbol(const TargetMachine &TM,
                                 MachineModuleInfo *MMI, const MCSymbol *RawSym,
                                 AArch64PACKey::ID Key,
                                 uint16_t Discriminator) const;
};

/// AArch64_MachoTargetObjectFile - This TLOF implementation is used for Darwin.
class AArch64_MachoTargetObjectFile : public TargetLoweringObjectFileMachO {
public:
  AArch64_MachoTargetObjectFile();

  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;

  const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                          const MCSymbol *Sym,
                                          const MCValue &MV, int64_t Offset,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         const TargetMachine &TM) const override;

  MCSymbol *getAuthPtrSlotSymbol(const TargetMachine &TM,
                                 MachineModuleInfo *MMI, const MCSymbol *RawSym,
                                 AArch64PACKey::ID Key,
                                 uint16_t Discriminator) const;
};

/// This implementation is used for AArch64 COFF targets.
class AArch64_COFFTargetObjectFile : public TargetLoweringObjectFileCOFF {};

} // end namespace llvm

#endif
