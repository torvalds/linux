//===-- XtensaMCAsmBackend.cpp - Xtensa assembler backend -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/XtensaFixupKinds.h"
#include "MCTargetDesc/XtensaMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace llvm {
class MCObjectTargetWriter;
class XtensaMCAsmBackend : public MCAsmBackend {
  uint8_t OSABI;
  bool IsLittleEndian;

public:
  XtensaMCAsmBackend(uint8_t osABI, bool isLE)
      : MCAsmBackend(llvm::endianness::little), OSABI(osABI),
        IsLittleEndian(isLE) {}

  unsigned getNumFixupKinds() const override {
    return Xtensa::NumTargetFixupKinds;
  }
  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;
  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;
  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override;
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter> createObjectTargetWriter() const override {
    return createXtensaObjectWriter(OSABI, IsLittleEndian);
  }
};
} // namespace llvm

const MCFixupKindInfo &
XtensaMCAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[Xtensa::NumTargetFixupKinds] = {
      // name                     offset bits  flags
      {"fixup_xtensa_branch_6", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_xtensa_branch_8", 16, 8, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_xtensa_branch_12", 12, 12, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_xtensa_jump_18", 6, 18, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_xtensa_call_18", 6, 18,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_xtensa_l32r_16", 8, 16,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits}};

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);
  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

static uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                                 MCContext &Ctx) {
  unsigned Kind = Fixup.getKind();
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
    return Value;
  case Xtensa::fixup_xtensa_branch_6: {
    Value -= 4;
    if (!isInt<6>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    unsigned Hi2 = (Value >> 4) & 0x3;
    unsigned Lo4 = Value & 0xf;
    return (Hi2 << 4) | (Lo4 << 12);
  }
  case Xtensa::fixup_xtensa_branch_8:
    Value -= 4;
    if (!isInt<8>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return (Value & 0xff);
  case Xtensa::fixup_xtensa_branch_12:
    Value -= 4;
    if (!isInt<12>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return (Value & 0xfff);
  case Xtensa::fixup_xtensa_jump_18:
    Value -= 4;
    if (!isInt<18>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return (Value & 0x3ffff);
  case Xtensa::fixup_xtensa_call_18:
    Value -= 4;
    if (!isInt<20>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 4-byte aligned");
    return (Value & 0xffffc) >> 2;
  case Xtensa::fixup_xtensa_l32r_16:
    unsigned Offset = Fixup.getOffset();
    if (Offset & 0x3)
      Value -= 4;
    if (!isInt<18>(Value) && (Value & 0x20000))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 4-byte aligned");
    return (Value & 0x3fffc) >> 2;
  }
}

static unsigned getSize(unsigned Kind) {
  switch (Kind) {
  default:
    return 3;
  case MCFixupKind::FK_Data_4:
    return 4;
  case Xtensa::fixup_xtensa_branch_6:
    return 2;
  }
}

void XtensaMCAsmBackend::applyFixup(const MCAssembler &Asm,
                                    const MCFixup &Fixup, const MCValue &Target,
                                    MutableArrayRef<char> Data, uint64_t Value,
                                    bool IsResolved,
                                    const MCSubtargetInfo *STI) const {
  MCContext &Ctx = Asm.getContext();
  MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());

  Value = adjustFixupValue(Fixup, Value, Ctx);

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  if (!Value)
    return; // Doesn't change encoding.

  unsigned Offset = Fixup.getOffset();
  unsigned FullSize = getSize(Fixup.getKind());

  for (unsigned i = 0; i != FullSize; ++i) {
    Data[Offset + i] |= uint8_t((Value >> (i * 8)) & 0xff);
  }
}

bool XtensaMCAsmBackend::mayNeedRelaxation(const MCInst &Inst,
                                           const MCSubtargetInfo &STI) const {
  return false;
}

void XtensaMCAsmBackend::relaxInstruction(MCInst &Inst,
                                          const MCSubtargetInfo &STI) const {}

bool XtensaMCAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                      const MCSubtargetInfo *STI) const {
  uint64_t NumNops24b = Count / 3;

  for (uint64_t i = 0; i != NumNops24b; ++i) {
    // Currently just little-endian machine supported,
    // but probably big-endian will be also implemented in future
    if (IsLittleEndian) {
      OS.write("\xf0", 1);
      OS.write("\x20", 1);
      OS.write("\0x00", 1);
    } else {
      report_fatal_error("Big-endian mode currently is not supported!");
    }
    Count -= 3;
  }

  // TODO maybe function should return error if (Count > 0)
  switch (Count) {
  default:
    break;
  case 1:
    OS.write("\0", 1);
    break;
  case 2:
    // NOP.N instruction
    OS.write("\x3d", 1);
    OS.write("\xf0", 1);
    break;
  }

  return true;
}

MCAsmBackend *llvm::createXtensaMCAsmBackend(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             const MCRegisterInfo &MRI,
                                             const MCTargetOptions &Options) {
  uint8_t OSABI =
      MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS());
  return new llvm::XtensaMCAsmBackend(OSABI, true);
}
