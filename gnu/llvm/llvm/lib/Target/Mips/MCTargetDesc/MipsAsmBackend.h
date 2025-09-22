//===-- MipsAsmBackend.h - Mips Asm Backend  ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MipsAsmBackend class.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSASMBACKEND_H
#define LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSASMBACKEND_H

#include "MCTargetDesc/MipsFixupKinds.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class MCAssembler;
struct MCFixupKindInfo;
class MCRegisterInfo;
class Target;

class MipsAsmBackend : public MCAsmBackend {
  Triple TheTriple;
  bool IsN32;

public:
  MipsAsmBackend(const Target &T, const MCRegisterInfo &MRI, const Triple &TT,
                 StringRef CPU, bool N32)
      : MCAsmBackend(TT.isLittleEndian() ? llvm::endianness::little
                                         : llvm::endianness::big),
        TheTriple(TT), IsN32(N32) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;
  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  unsigned getNumFixupKinds() const override {
    return Mips::NumTargetFixupKinds;
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override;

  bool isMicroMips(const MCSymbol *Sym) const override;
}; // class MipsAsmBackend

} // namespace

#endif
