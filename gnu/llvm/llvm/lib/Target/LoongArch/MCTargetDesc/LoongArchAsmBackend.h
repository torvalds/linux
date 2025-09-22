//===-- LoongArchAsmBackend.h - LoongArch Assembler Backend ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LoongArchAsmBackend class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHASMBACKEND_H
#define LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHASMBACKEND_H

#include "MCTargetDesc/LoongArchBaseInfo.h"
#include "MCTargetDesc/LoongArchFixupKinds.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {

class LoongArchAsmBackend : public MCAsmBackend {
  const MCSubtargetInfo &STI;
  uint8_t OSABI;
  bool Is64Bit;
  const MCTargetOptions &TargetOptions;
  DenseMap<MCSection *, const MCSymbolRefExpr *> SecToAlignSym;

public:
  LoongArchAsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI, bool Is64Bit,
                      const MCTargetOptions &Options)
      : MCAsmBackend(llvm::endianness::little,
                     LoongArch::fixup_loongarch_relax),
        STI(STI), OSABI(OSABI), Is64Bit(Is64Bit), TargetOptions(Options) {}
  ~LoongArchAsmBackend() override {}

  bool handleAddSubRelocations(const MCAssembler &Asm, const MCFragment &F,
                               const MCFixup &Fixup, const MCValue &Target,
                               uint64_t &FixedValue) const override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  // Return Size with extra Nop Bytes for alignment directive in code section.
  bool shouldInsertExtraNopBytesForCodeAlign(const MCAlignFragment &AF,
                                             unsigned &Size) override;

  // Insert target specific fixup type for alignment directive in code section.
  bool shouldInsertFixupForCodeAlign(MCAssembler &Asm,
                                     MCAlignFragment &AF) override;

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override;

  unsigned getNumFixupKinds() const override {
    return LoongArch::NumTargetFixupKinds;
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {}

  std::pair<bool, bool> relaxLEB128(const MCAssembler &Asm, MCLEBFragment &LF,
                                    int64_t &Value) const override;

  bool relaxDwarfLineAddr(const MCAssembler &Asm, MCDwarfLineAddrFragment &DF,
                          bool &WasRelaxed) const override;
  bool relaxDwarfCFA(const MCAssembler &Asm, MCDwarfCallFrameFragment &DF,
                     bool &WasRelaxed) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;
  const MCTargetOptions &getTargetOptions() const { return TargetOptions; }
  DenseMap<MCSection *, const MCSymbolRefExpr *> &getSecToAlignSym() {
    return SecToAlignSym;
  }
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHASMBACKEND_H
